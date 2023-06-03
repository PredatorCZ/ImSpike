/*  ImSpike
    Copyright(C) 2023 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.If not, see <https://www.gnu.org/licenses/>.
*/

#include "spike/batch.hpp"
#include "datas/master_printer.hpp"
#include "main.hpp"
#include "spike/console.hpp"
#include <cinttypes>
#include <thread>

struct ProcessedFiles : LoadingBar, CounterLine {
  char buffer[128]{};

  ProcessedFiles() : LoadingBar({buffer, sizeof(buffer)}) {}
  void PrintLine() override {
    snprintf(buffer, sizeof(buffer), "Processed %4" PRIuMAX " files.",
             curitem.load(std::memory_order_relaxed));
    LoadingBar::PrintLine();
  }
};

struct ExtractStats {
  std::map<JenHash, size_t> archiveFiles;
  size_t totalFiles = 0;
};

struct UILines {
  ProgressBar *totalProgress{nullptr};
  CounterLine *totalCount{nullptr};
  std::map<uint32, ProgressBar *> bars;
  std::mutex barsMutex;

  auto ChooseBar() {
    if (bars.empty()) {
      return (ProgressBar *)(nullptr);
    }

    auto threadId = std::this_thread::get_id();
    auto id = reinterpret_cast<const uint32 &>(threadId);
    auto found = bars.find(id);

    if (es::IsEnd(bars, found)) {
      std::lock_guard<std::mutex> lg(barsMutex);
      auto retVal = bars.begin()->second;
      bars.emplace(id, retVal);
      bars.erase(bars.begin());

      return retVal;
    }

    return found->second;
  };

  UILines(const ExtractStats &stats) {
    ModifyElements([&](ElementAPI &api) {
      const size_t minThreads =
          std::min(size_t(std::thread::hardware_concurrency()),
                   stats.archiveFiles.size());

      if (minThreads < 2) {
        return;
      }

      for (size_t t = 0; t < minThreads; t++) {
        auto progBar = std::make_unique<ProgressBar>("Thread:");
        auto progBarRaw = progBar.get();
        bars.emplace(t, progBarRaw);
        api.Append(std::move(progBar));
      }
    });

    auto prog = AppendNewLogLine<DetailedProgressBar>("Total: ");
    prog->ItemCount(stats.totalFiles);
    totalCount = prog;
  }

  UILines(size_t totalInputFiles) {
    totalCount = AppendNewLogLine<ProcessedFiles>();
    auto prog = AppendNewLogLine<DetailedProgressBar>("Total: ");
    prog->ItemCount(totalInputFiles);
    totalProgress = prog;
  }

  ~UILines() {
    ModifyElements([&](ElementAPI &api) {
      if (totalCount) {
        // Wait a little bit for internal queues to finish printing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (totalProgress) {
          auto data = static_cast<ProcessedFiles *>(totalCount);
          data->Finish();
        }
      }
    });
  }
};

struct BatchQueueImpl;

struct ExtractStatsMaker : ExtractStats {
  std::mutex mtx;
  LoadingBar *scanBar;

  void Push(AppContextShare *ctx, size_t numFiles) {
    std::unique_lock<std::mutex> lg(mtx);
    archiveFiles.emplace(ctx->Hash(), numFiles);
    totalFiles += numFiles;
  }

  ~ExtractStatsMaker() { scanBar->Finish(); }
};

void ProcessBatch(BatchQueueImpl &batch, size_t numFiles);
std::shared_ptr<ExtractStatsMaker> ExtractStatBatch(BatchQueueImpl &batch);
void ProcessBatch(BatchQueueImpl &batch, ExtractStats *stats);
void PackModeBatch(BatchQueueImpl &batch);

struct BatchQueueImpl : QueueContext {
  void ProcessQueueInernal() {
    for (auto &q : queue) {
      const std::string fullPath = q.path0 + "/" + q.path1;

      if (q.isFolder) {
        scanner.Scan(fullPath);

        if (updateFileCount) {
          const size_t numFiles = scanner.Files().size();
          updateFileCount(numFiles ? numFiles - 1 : 0);
        }

        if (forEachFolder) {
          AppPackStats stats{};
          stats.numFiles = scanner.Files().size();

          for (auto &f : scanner) {
            stats.totalSizeFileNames += f.size() + 1;
          }

          forEachFolder(fullPath, stats);
        }

        for (auto &f : scanner) {
          manager.Push([&, iCtx{MakeIOContext(f)}] {
            forEachFile(iCtx.get());
            iCtx->Finish();
          });
        }

        manager.Wait();

        if (forEachFolderFinish) {
          forEachFolderFinish();
        }
      } else {
        manager.Push([&, iCtx{MakeIOContext(fullPath)}] {
          forEachFile(iCtx.get());
          iCtx->Finish();
        });
      }
    }

    Clean();
  }

  void ProcessQueue() override {
    if (ctx->NewArchive) {
      PackModeBatch(*this);
    } else {
      if (ctx->ExtractStat) {
        auto stats = ExtractStatBatch(*this);
        ProcessQueueInernal();
        stats.get()->totalFiles += queue.size();
        ProcessBatch(*this, stats.get());
      } else {
        ProcessBatch(*this, queue.size());
      }
    }

    ProcessQueueInernal();
  }

  void Clean() {
    manager.Wait();
    scanner.Clear();
    es::Dispose(forEachFile);
    es::Dispose(forEachFolderFinish);
    es::Dispose(forEachFolder);
    es::Dispose(updateFileCount);
  }

  BatchQueueImpl(APPContext *ctx_, size_t queueCapacity)
      : ctx(ctx_), manager(queueCapacity) {
    for (auto &c : ctx->info->filters) {
      scanner.AddFilter(c);
    }
  }

  APPContext *ctx;
  WorkerManager manager{0};
  DirectoryScanner scanner;

  std::function<void(const std::string &path, AppPackStats)> forEachFolder;
  std::function<void()> forEachFolderFinish;
  std::function<void(AppContextShare *)> forEachFile;
  std::function<void(size_t)> updateFileCount;
};

void PackModeBatch(BatchQueueImpl &batch) {
  struct PackData {
    size_t index = 0;
    std::unique_ptr<AppPackContext> archiveContext;
    std::string pbarLabel;
    DetailedProgressBar *progBar = nullptr;
    std::string folderPath;
  };

  auto payload = std::make_shared<PackData>();

  batch.forEachFolder = [payload, ctx = batch.ctx](const std::string &path,
                                                   AppPackStats stats) {
    payload->folderPath = path;
    payload->archiveContext.reset(ctx->NewArchive(path, stats));
    payload->pbarLabel = "Folder id " + std::to_string(payload->index++);
    payload->progBar =
        AppendNewLogLine<DetailedProgressBar>(payload->pbarLabel);
    payload->progBar->ItemCount(stats.numFiles);
    printline("Processing: " << path);
  };

  batch.forEachFile = [payload](AppContextShare *iCtx) {
    payload->archiveContext->SendFile(
        iCtx->workingFile.GetFullPath().substr(payload->folderPath.size() + 1),
        iCtx->GetStream());
    (*payload->progBar)++;
  };

  batch.forEachFolderFinish = [payload] {
    payload->archiveContext->Finish();
    payload->archiveContext.reset();
  };
}

std::shared_ptr<ExtractStatsMaker> ExtractStatBatch(BatchQueueImpl &batch) {
  auto sharedData = std::make_shared<ExtractStatsMaker>();
  sharedData->scanBar =
      AppendNewLogLine<LoadingBar>("Processing extract stats.");

  batch.forEachFile = [payload = sharedData,
                       ctx = batch.ctx](AppContextShare *iCtx) {
    payload->Push(iCtx, ctx->ExtractStat(std::bind(
                            [&](size_t offset, size_t size) {
                              return iCtx->GetBuffer(size, offset);
                            },
                            std::placeholders::_1, std::placeholders::_2)));
  };

  return sharedData;
}

void ProcessBatch(BatchQueueImpl &batch, ExtractStats *stats) {
  batch.forEachFile = [payload = std::make_shared<UILines>(*stats),
                       archiveFiles =
                           std::make_shared<decltype(stats->archiveFiles)>(
                               std::move(stats->archiveFiles)),
                       ctx = batch.ctx](AppContextShare *iCtx) {
    auto currentBar = payload->ChooseBar();
    if (currentBar) {
      currentBar->ItemCount(archiveFiles->at(iCtx->Hash()));
    }

    iCtx->forEachFile = [=] {
      if (currentBar) {
        (*currentBar)++;
      }

      if (payload->totalCount) {
        (*payload->totalCount)++;
      }
    };

    printline("Processing: " << iCtx->FullPath());
    ctx->ProcessFile(iCtx);
    if (payload->totalProgress) {
      (*payload->totalProgress)++;
    }
  };
}

void ProcessBatch(BatchQueueImpl &batch, size_t numFiles) {
  auto payload = std::make_shared<UILines>(numFiles);
  batch.forEachFile = [payload = payload,
                       ctx = batch.ctx](AppContextShare *iCtx) {
    printline("Processing: " << iCtx->FullPath());
    ctx->ProcessFile(iCtx);
    if (payload->totalProgress) {
      (*payload->totalProgress)++;
    }
    if (payload->totalCount) {
      (*payload->totalCount)++;
    }
  };

  auto totalFiles = std::make_shared<size_t>(numFiles);
  batch.updateFileCount = [payload = payload,
                           totalFiles = totalFiles](size_t addedFiles) {
    *totalFiles.get() += addedFiles;
    payload->totalProgress->ItemCount(*totalFiles);
  };
}

std::shared_ptr<QueueContext> MakeWorkerContext(APPContext *ctx) {
  return std::make_shared<BatchQueueImpl>(ctx, ctx->info->multithreaded * 50);
}

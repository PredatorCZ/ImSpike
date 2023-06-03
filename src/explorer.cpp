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

#include "ImGuiFileDialog.h"
#include "datas/jenkinshash.hpp"
#include "font_awesome4/definitions.h"
#include "imgui_internal.h"
#include "main.hpp"
#include <algorithm>
#include <filesystem>

namespace {
void DrawFile(ImVec2 rectMin, ImVec2 rectMax, ImVec2 rectSize, ImColor color,
              std::string_view extension) {
  float minRectSize = std::min(rectSize.x, rectSize.y);

  ImVec2 rectBegin = rectMin;
  rectBegin.x += (rectSize.x - minRectSize) / 1.5;
  ImVec2 rectEnd = rectMax;
  rectEnd.x -= (rectSize.x - minRectSize) / 1.5;

  auto drawList = ImGui::GetWindowDrawList();

  // Draw file body
  const ImVec2 points[]{
      {rectBegin},
      {rectBegin.x, rectEnd.y},
      {rectEnd},
      {rectEnd.x, rectEnd.y - minRectSize / 1.5f},
      {rectEnd.x - minRectSize / 3, rectBegin.y},
  };

  drawList->AddConvexPolyFilled(points, 5, 0xff000000 | color);
  ImColor invColor(0xff000000 | ~color);

  // Draw folded corner
  drawList->AddTriangleFilled(points[3], points[4], {points[4].x, points[3].y},
                              invColor);

  // Draw extension
  if (!extension.empty()) {
    ImVec2 rectBegin_{rectMin.x + 4, rectEnd.y - minRectSize / 2.25f};
    ImVec2 rectEnd_{rectEnd.x - minRectSize / 3, rectEnd.y - minRectSize / 5};
    drawList->AddRectFilled(rectBegin_, rectEnd_,
                            0xff000000 | ImGui::GetColorU32(ImGuiCol_FrameBg),
                            100);
    float textSize = rectEnd_.y - rectBegin_.y;
    ImVec2 textBegin{rectBegin_.x + textSize / 4, rectBegin_.y};
    ImVec4 textClip{textBegin.x, textBegin.y, rectEnd_.x - textSize / 4,
                    rectEnd_.y};
    drawList->AddText(nullptr, textSize, textBegin, ~0, extension.data(),
                      extension.data() + extension.size(), 0.f, &textClip);
  }
}

void DrawFolder(ImVec2 rectMin, ImVec2 rectMax, ImVec2 rectSize, bool empty) {
  const float padding = 6;
  const ImVec2 padding_{padding, padding};
  auto drawList = ImGui::GetWindowDrawList();

  {
    const ImVec2 points[]{
        {rectMin.x + padding, rectMax.y},
        ImVec2{rectMin} + padding_,
        {rectMin.x + rectSize.x / 2.5f, rectMin.y + padding},
        {rectMin.x + rectSize.x / 2, rectMin.y + padding + rectSize.y / 10},
        {rectMax.x - padding, rectMin.y + padding + rectSize.y / 10},
        {rectMax.x - padding, rectMax.y},
    };

    drawList->AddConvexPolyFilled(
        points, 6, 0xff000000 | ImGui::GetColorU32(ImGuiCol_FrameBg));
  }

  if (!empty) {
    drawList->AddRectFilled(
        {rectMin.x + padding + padding / 2, rectMax.y - rectSize.y / 1.4f},
        {rectMax.x - padding - padding / 2, rectMax.y - rectSize.y / 2},
        ImGui::GetColorU32(ImGuiCol_Text));
  }

  {
    const ImVec2 points[]{
        {rectMax.x - padding, rectMax.y},
        {rectMax.x - padding, rectMax.y - rectSize.y / 1.75f},
        {rectMin.x + rectSize.x / 2, rectMax.y - rectSize.y / 1.75f},
        {rectMin.x + rectSize.x / 2.5f, rectMax.y - rectSize.y / 2},
        {rectMin.x + padding, rectMax.y - rectSize.y / 2},
        {rectMin.x + padding, rectMax.y},
    };

    drawList->AddConvexPolyFilled(
        points, 6, 0xff000000 | ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
  }
}

struct ExplorerItem {
  std::string name;
};

struct ExplorerFile : ExplorerItem {
  std::string extension;
  ImColor color;
};

struct ExplorerItems {
  std::vector<ExplorerFile> files;
  std::vector<ExplorerItem> folders;
  std::vector<bool> emptyFolders;
  std::vector<bool> selectedItems;
};

struct FolderTree {
  std::filesystem::path fullPath;
  std::string folderName;
  std::vector<std::unique_ptr<FolderTree>> children;
  bool scanned = false;
};

struct MountManagerImpl : MountManager {
  std::vector<std::filesystem::path> systemMounts;
  std::vector<FolderTree> rootTree;
  std::filesystem::path *currentMount = nullptr;
  std::filesystem::path currentPath;
  ExplorerItems items;
  std::vector<bool> selectedMounts;

  void AddMount(std::string path) override;
};

ExplorerItems ScanFolder(std::filesystem::path path,
                         ExplorerItems retVal = {}) {
  namespace fs = std::filesystem;
  fs::directory_iterator dirIt(path);

  for (auto &entry : dirIt) {
    if (entry.is_regular_file()) {
      std::string entryPath(entry.path().filename());
      std::string extension(entry.path().extension());
      ImColor extColor = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
      if (!extension.empty()) {
        extension.erase(extension.begin());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](char c) { return std::toupper(c); });
        extColor = JenkinsHash_(extension);
      }

      retVal.files.emplace_back(
          ExplorerFile{std::move(entryPath), std::move(extension), extColor});
    } else if (entry.is_directory()) {
      try {
        fs::directory_iterator subDirIt(entry);
        std::string fileName(entry.path().filename());
        auto found = std::lower_bound(
            retVal.folders.begin(), retVal.folders.end(), fileName,
            [](auto &item, auto &fileName) { return item.name < fileName; });

        retVal.emptyFolders.insert(
            std::next(retVal.emptyFolders.begin(),
                      std::distance(retVal.folders.begin(), found)),
            subDirIt == fs::end(subDirIt));
        retVal.folders.insert(found, ExplorerItem{entry.path().filename()});
      } catch (...) {
      }
    }
  }

  std::sort(retVal.files.begin(), retVal.files.end(),
            [](auto &i0, auto &i1) { return i0.name < i1.name; });
  retVal.selectedItems.resize(retVal.files.size() + retVal.folders.size());

  return retVal;
}

void Rescan(MountManagerImpl &man, std::filesystem::path path) {
  auto RescanRoot = [&] {
    man.items = {};
    man.currentMount = nullptr;
    man.currentPath = std::filesystem::path{};
    man.rootTree.clear();

    for (auto &m : man.systemMounts) {
      man.items = ScanFolder(m, std::move(man.items));
    }

    for (auto &i : man.items.folders) {
      FolderTree rootTree{
          .folderName = i.name,
      };

      for (auto &m : man.systemMounts) {
        if (std::filesystem::exists(m / i.name)) {
          rootTree.fullPath = m / i.name;
          break;
        }
      }

      man.rootTree.emplace_back(std::move(rootTree));
    }
  };

  // We entered root for current mount
  if (path.empty()) {
    RescanRoot();
    return;
  }

  // We're in root
  if (!man.currentMount) {
    for (auto &m : man.systemMounts) {
      if (std::filesystem::exists(m / path)) {
        man.currentMount = &m;
        break;
      }
    }
  }

  man.currentPath = path;
  man.items = ScanFolder(*man.currentMount / path);
}

void MountManagerImpl::AddMount(std::string path) {
  systemMounts.emplace_back(std::move(path));
  selectedMounts.emplace_back(false);
  Rescan(*this, {});
}

void ScanTree(FolderTree &tree) {
  namespace fs = std::filesystem;
  fs::directory_iterator dirIt(tree.fullPath);
  tree.scanned = true;

  for (auto &entry : dirIt) {
    if (entry.is_directory()) {
      tree.children.emplace_back(std::make_unique<FolderTree>(FolderTree{
          .fullPath = entry,
          .folderName = entry.path().filename(),
      }));
    }
  }
}

void DrawFolderTree(FolderTree &tree, uint32 level, uint32 index,
                    std::filesystem::path &selectedPath) {
  uint64 ptrId = index | (uint64(level) >> 32);
  const bool opened =
      ImGui::TreeNode(reinterpret_cast<void *>(ptrId), ICON_FA_FOLDER " %s",
                      tree.folderName.c_str());
  if (opened && !tree.scanned) {
    ScanTree(tree);
  }

  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    selectedPath = tree.fullPath;
  }

  if (opened) {
    uint32 childIndex = 0;

    for (auto &f : tree.children) {
      DrawFolderTree(*f, level + 1, childIndex++, selectedPath);
    }

    ImGui::TreePop();
  } else if (tree.scanned) {
    tree.scanned = false;
    tree.children.clear();
  }
}
} // namespace

void ExplorerWindow(MountManager &man_, std::vector<Queue> &queue) {
  auto &man = static_cast<MountManagerImpl &>(man_);

  if (ImGui::Begin("File Explorer", nullptr,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse)) {
    if (ImGui::BeginTable("ExplorerWindowTbl", 2, ImGuiTableFlags_Resizable)) {
      ImGui::TableNextColumn();
      if (ImGui::BeginChild("TreeExplorer")) {
        uint32 childIndex = 0;
        std::filesystem::path selectedPath;
        for (auto &t : man.rootTree) {
          DrawFolderTree(t, 0, childIndex++, selectedPath);
        }

        if (!selectedPath.empty()) {
          for (auto &m : man.systemMounts) {
            if (auto relPath = std::filesystem::relative(selectedPath, m);
                !relPath.empty() && !relPath.native().starts_with("..")) {
              man.currentMount = &m;
              Rescan(man, relPath);
              break;
            }
          }
        }
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      if (ImGui::BeginChild("FoldersExplorer"))
        [&] {
          if (ImGui::Button("Add selected items to queue", {0, 25})) {
            size_t curItem = 0;
            for (auto i : man.items.selectedItems) {
              if (i) {
                const bool isFolder = curItem < man.items.folders.size();
                const std::string &itemPath =
                    man.currentPath /
                    (isFolder ? man.items.folders.at(curItem)
                              : man.items.files.at(curItem -
                                                   man.items.folders.size()))
                        .name;
                // We're in root
                if (!man.currentMount) {
                  for (auto &m : man.systemMounts) {
                    if (std::filesystem::exists(m / itemPath)) {
                      man.currentMount = &m;
                      break;
                    }
                  }
                }

                queue.emplace_back(Queue{.path0 = man.currentMount->string(),
                                         .path1 = itemPath,
                                         .isFolder = isFolder});
                man.items.selectedItems[curItem] = false;
              }
              curItem++;
            }
          }

          ImGui::SameLine();
          if (ImGui::Button(ICON_FA_LEVEL_UP, {25, 25})) {
            Rescan(man, man.currentPath.parent_path());
          }

          ImGui::SameLine();
          ImGui::TextUnformatted(man.currentPath.c_str());

          auto &curStyle = ImGui::GetStyle();
          const int width = ImGui::GetWindowWidth() - curStyle.ScrollbarSize;
          const int entrySize = 60;
          const int numXItems = std::max(1, width / (entrySize + 8));
          const float restPadding =
              (width % (entrySize + 8)) / float(numXItems);
          ImVec2 selectedPos;
          float selectedWidth = 0;
          static int selectIndex = -1;
          if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selectIndex = -1;
          }

          size_t curItem = 0;

          auto PushItem = [&](auto &varItem) {
            if (curItem % numXItems) {
              ImGui::SameLine();
            }

            auto label = "##slectable_" + std::to_string(curItem);
            if (ImGui::Selectable(
                    label.c_str(), man.items.selectedItems[curItem],
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(entrySize + restPadding, entrySize + restPadding))) {
              if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (selectIndex < man.items.folders.size()) {
                  selectIndex = -1;
                  Rescan(man, man.currentPath / varItem.name);
                  return true;
                }
              } else {
                selectIndex = curItem;
                man.items.selectedItems[curItem] =
                    !man.items.selectedItems[curItem];
              }
            }

            auto curWindow = ImGui::GetCurrentWindow();

            ImVec2 wpos = curWindow->Pos - curWindow->Scroll;
            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImVec2 textBegin = wpos;
            textBegin.x += cursorPos.x + (curItem % numXItems) *
                                             (entrySize + restPadding + 8);
            textBegin.y += cursorPos.y - entrySize / 3;
            ImVec2 rectMin = ImGui::GetItemRectMin();
            ImVec2 rectMax = ImGui::GetItemRectMax();

            ImVec2 rectSize{ImGui::GetItemRectSize().x,
                            textBegin.y - rectMin.y};
            ImVec2 rectMax_{rectMax.x, textBegin.y};

            if (selectIndex != curItem) [[likely]] {
              ImVec2 textEnd = wpos;
              textEnd.x = textBegin.x + entrySize + restPadding;
              textEnd.y += cursorPos.y;
              ImGui::RenderTextClipped(
                  textBegin, textEnd, varItem.name.data(),
                  varItem.name.data() + varItem.name.size(), nullptr);
            } else {
              selectedPos.x = rectMin.x;
              selectedPos.y = rectMax_.y;
              selectedWidth = rectMax.y - rectMin.y;
            }

            if constexpr (std::is_same_v<std::decay_t<decltype(varItem)>,
                                         ExplorerFile>) {
              DrawFile(rectMin, rectMax_, rectSize, varItem.color,
                       varItem.extension);
            } else {
              DrawFolder(rectMin, rectMax_, rectSize,
                         man.items.emptyFolders.at(curItem));
            }

            curItem++;
            return false;
          };

          for (auto &folder : man.items.folders) {
            if (PushItem(folder)) {
              return;
            }
          }

          for (auto &file : man.items.files) {
            if (PushItem(file)) {
              return;
            }
          }

          if (selectIndex > -1) {
            std::string_view label(
                (selectIndex >= man.items.folders.size()
                     ? man.items.files.at(selectIndex -
                                          man.items.folders.size())
                     : man.items.folders.at(selectIndex))
                    .name);

            auto textSize =
                ImGui::CalcTextSize(label.data(), label.data() + label.size(),
                                    false, entrySize + restPadding);
            auto drawList = ImGui::GetWindowDrawList();
            auto textBegin = selectedPos;
            textBegin.x += 4;
            auto textBgBegin = selectedPos;
            textBgBegin.y += ImGui::GetFontSize();
            auto textBgEnd = textBgBegin;
            textBgEnd.x += selectedWidth + 4;
            textBgEnd.y += textSize.y - ImGui::GetFontSize();
            drawList->AddRectFilled(textBgBegin, textBgEnd,
                                    0xff000000 |
                                        ImGui::GetColorU32(ImGuiCol_FrameBg));
            ImGui::RenderTextWrapped(textBegin, label.data(),
                                     label.data() + label.size(),
                                     entrySize + restPadding);
          }
        }();

      ImGui::EndChild();
      ImGui::EndTable();
    }
  }

  ImGui::End();
}

void MountsWindow(MountManager &man_) {
  auto &mounts = static_cast<MountManagerImpl &>(man_);
  if (ImGui::Begin("Mounts", nullptr, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::Button("Add path")) {
      ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey",
                                              "Choose Folder", nullptr, "");
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey",
                                             ImGuiWindowFlags_NoCollapse |
                                                 ImGuiWindowFlags_NoDocking,
                                             {512, 256})) {
      if (ImGuiFileDialog::Instance()->IsOk()) {
        std::string filePathName =
            ImGuiFileDialog::Instance()->GetFilePathName();
        mounts.systemMounts.emplace_back(
            ImGuiFileDialog::Instance()->GetFilePathName());
        mounts.selectedMounts.resize(mounts.systemMounts.size());
        Rescan(mounts, {});
      }
      ImGuiFileDialog::Instance()->Close();
    }

    ImGui::SameLine();
    if (ImGui::Button("Remove selected paths")) {
      std::vector<std::filesystem::path> newMounts(
          std::move(mounts.systemMounts));

      size_t curMount = 0;
      for (auto &q : newMounts) {
        if (!mounts.selectedMounts.at(curMount++)) {
          mounts.systemMounts.emplace_back(std::move(q));
        }
      }

      Rescan(mounts, {});
    }

    if (ImGui::BeginTable("MountsTable", 1, ImGuiTableFlags_Borders)) {
      size_t curMount = 0;
      for (auto &m : mounts.systemMounts) {
        ImGui::TableNextColumn();
        auto bref = mounts.selectedMounts.at(curMount++);
        ImGui::Selectable(m.c_str(), bref);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
          bref.flip();
        }
      }
    }
    ImGui::EndTable();
  }

  ImGui::End();
}

std::unique_ptr<MountManager> CreateMountsContext() {
  return std::make_unique<MountManagerImpl>();
}

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
#include "datas/directory_scanner.hpp"
#include "datas/master_printer.hpp"
#include "datas/reflector.hpp"
#include "font_awesome4/definitions.h"
#include "imgui_internal.h"
#include "main.hpp"
#include "spike/console.hpp"
#include "spike/context.hpp"
#include <cinttypes>
#include <future>
#include <sstream>

struct ReflectedInstanceFriend : ReflectedInstance {
  const reflectorStatic *Refl() const { return rfStatic; }
  void *Instance() { return instance; }
};

class ReflectorFriend : public Reflector {
public:
  using Reflector::GetReflectedInstance;
  using Reflector::GetReflectedType;
  using Reflector::SetReflectedValue;
};

namespace {
struct ModuleInfo {
  std::string folder;
  std::string header;
  std::string module;
  std::string_view descrVersion;
  std::string_view copyright;

  ModuleInfo(std::string str, std::string folder_, std::string module_)
      : folder(std::move(folder_)), header(std::move(str)),
        module(std::move(module_)) {
    const size_t foundComma = header.find_first_of(',');

    if (foundComma != header.npos) {
      header[foundComma] = 0;
      descrVersion =
          es::TrimWhitespace(std::string_view(header.data(), foundComma));
      copyright =
          es::TrimWhitespace(std::string_view(header.data() + foundComma + 1));
    } else {
      descrVersion = es::TrimWhitespace(std::string_view(header));
    }
  }
};

std::vector<ModuleInfo> ScanModules(const std::string &appFolder,
                                    const std::string &appName) {
  std::map<std::string, ModuleInfo> modules;
  DirectoryScanner sc;
  sc.AddFilter(std::string_view(".spk$"));
  sc.Scan(appFolder);

  for (auto &m : sc) {
    try {
      AFileInfo modulePath(m);
      auto moduleName = modulePath.GetFilename();
      const size_t firstDotPos = moduleName.find_first_of('.');
      std::string moduleNameStr(moduleName.substr(0, firstDotPos));

      if (!modules.contains(moduleNameStr)) {
        APPContext ctx(moduleNameStr.data(), appFolder, appName);
        modules.emplace(moduleNameStr,
                        ModuleInfo(std::string(ctx.info->header),
                                   std::string(modulePath.GetFolder()),
                                   moduleNameStr));
      }
    } catch (const std::runtime_error &e) {
      printerror(e.what());
    }
  }

  std::vector<ModuleInfo> retval;
  std::transform(modules.begin(), modules.end(), std::back_inserter(retval),
                 [](auto &item) { return std::move(item.second); });

  return retval;
}

static auto &MainSettings() {
  static ReflectorWrap<MainAppConfFriend> wrap(mainSettings);
  return reinterpret_cast<ReflectorFriend &>(wrap);
}

static auto &CliSettings() {
  static ReflectorWrap<CLISettings> wrap(cliSettings);
  return reinterpret_cast<ReflectorFriend &>(wrap);
}

static ReflectedInstanceFriend RTInstance(const ReflectorFriend &ref) {
  auto rawRTTI = ref.GetReflectedInstance();
  return static_cast<ReflectedInstanceFriend>(rawRTTI);
}

std::string TransformTypeName(std::string_view typeName) {
  std::string retVal;
  bool nextUpperCase = true;

  for (auto c : typeName) {
    if (c == '-') {
      retVal.push_back(' ');
      nextUpperCase = true;
      continue;
    }

    if (nextUpperCase) {
      retVal.push_back(std::toupper(c));
      nextUpperCase = false;
      continue;
    }

    retVal.push_back(c);
  }

  return retVal;
}

std::vector<std::string_view> Explode(std::string_view str) {
  while (str.front() == ',') {
    str.remove_prefix(1);
  }

  std::vector<std::string_view> resVal;
  const char *lastOffset = str.data();
  const size_t fullSize = str.size();
  const char *pathEnd = lastOffset + fullSize;

  for (size_t c = 1; c < fullSize; c++)
    if (str[c] == ',') {
      resVal.emplace_back(
          es::TrimWhitespace(std::string_view(lastOffset, str.data() + c)));
      lastOffset = str.data() + c + 1;
    }

  if (lastOffset != pathEnd) {
    resVal.emplace_back(es::TrimWhitespace(std::string_view(lastOffset)));
  }

  return resVal;
}

struct Options {
  bool hidden = false;
  bool min = false;
  bool max = false;
  bool filePath = false;
  bool folder = false;
  mutable uint8 hiddenState = 0;
  mutable int selected = 0;
  std::string fileFilter;
  ImGuiDataType dataType = ImGuiDataType_COUNT;
  const char *format = nullptr;
  union {
    float minf;
    uint64 minu = 0;
    int64 mins;
    double mind;
  };

  union {
    float maxf;
    double maxd;
    uint64 maxu = 0;
  };
};

ImGuiDataType RefTypeToImGuiDataType(REFType type, uint16 typeSize) {
  switch (type) {
  case REFType::Integer: {
    switch (typeSize) {
    case 1:
      return ImGuiDataType_S8;

    case 2:
      return ImGuiDataType_S16;

    case 4:
      return ImGuiDataType_S32;

    case 8:
      return ImGuiDataType_S64;
    }
    break;
  }
  case REFType::UnsignedInteger: {
    switch (typeSize) {
    case 1:
      return ImGuiDataType_U8;

    case 2:
      return ImGuiDataType_U16;

    case 4:
      return ImGuiDataType_U32;

    case 8:
      return ImGuiDataType_U64;
    }
    break;
  }
  case REFType::FloatingPoint: {
    switch (typeSize) {
    case 4:
      return ImGuiDataType_Float;

    case 8:
      return ImGuiDataType_Double;
    }
    break;
  }
  }

  return ImGuiDataType_COUNT;
}

const char *GetFormat(ImGuiDataType type) {
  switch (type) {
  case ImGuiDataType_S8:
    return "%" PRIi8;

  case ImGuiDataType_S16:
    return "%" PRIi16;

  case ImGuiDataType_S32:
    return "%" PRIi32;

  case ImGuiDataType_S64:
    return "%" PRIi64;

  case ImGuiDataType_U8:
    return "%" PRIu8;

  case ImGuiDataType_U16:
    return "%" PRIu16;

  case ImGuiDataType_U32:
    return "%" PRIu32;

  case ImGuiDataType_U64:
    return "%" PRIu64;

  case ImGuiDataType_Float:
    return "%" PRId32;

  case ImGuiDataType_Double:
    return "%" PRId64;
  }

  return nullptr;
}

Options GetOptions(std::string_view descr, REFType type, uint16 typeSize) {
  std::vector<std::string_view> exploded;
  if (!descr.empty()) {
    exploded = Explode(descr);
  }
  Options retval;
  retval.dataType = RefTypeToImGuiDataType(type, typeSize);
  retval.format = GetFormat(retval.dataType);

  for (auto e : exploded) {
    if (e == "HIDDEN") {
      retval.hidden = true;
    } else if (e.starts_with("MIN:")) {
      auto minData = es::SkipStartWhitespace(e.substr(4));
      retval.min = true;

      switch (type) {
      case REFType::Integer:
        retval.mins = atoll(minData.data());
        break;
      case REFType::UnsignedInteger:
        retval.minu = strtoull(minData.data(), nullptr, 10);
        break;
      case REFType::FloatingPoint: {
        if (typeSize == 4) {
          retval.minf = atof(minData.data());
        } else {
          retval.mind = atof(minData.data());
        }
        break;
      }

      default:
        break;
      }
    } else if (e.starts_with("MAX:")) {
      auto maxData = es::SkipStartWhitespace(e.substr(4));
      retval.max = true;

      switch (type) {
      case REFType::Integer:
      case REFType::UnsignedInteger:
        retval.maxu = strtoull(maxData.data(), nullptr, 10);
        break;
      case REFType::FloatingPoint: {
        if (typeSize == 4) {
          retval.maxf = atof(maxData.data());
        } else {
          retval.maxd = atof(maxData.data());
        }
        break;
      }

      default:
        break;
      }
    } else if (e.starts_with("FILEPATH")) {
      retval.filePath = true;
      auto filterData = e.substr(8);

      if (filterData[0] == ':') {
        filterData = es::SkipStartWhitespace(filterData.substr(1));
        retval.fileFilter = filterData;
        std::transform(retval.fileFilter.begin(), retval.fileFilter.end(),
                       retval.fileFilter.begin(),
                       [](char c) { return c == ';' ? ',' : c; });
      }
    } else if (e == "FOLDER") {
      retval.folder = true;
      retval.filePath = true;
    }
  }

  if (retval.filePath && !retval.folder && retval.fileFilter.empty()) {
    retval.fileFilter = ".*";
  }

  if (!retval.max) {
    switch (type) {
    case REFType::Integer: {
      switch (typeSize) {
      case 1:
        retval.maxu = std::numeric_limits<int8>::max();
        break;

      case 2:
        retval.maxu = std::numeric_limits<int16>::max();
        break;

      case 4:
        retval.maxu = std::numeric_limits<int32>::max();
        break;

      case 8:
        retval.maxu = std::numeric_limits<int64>::max();
        break;
      }
      break;
    }
    case REFType::UnsignedInteger: {
      switch (typeSize) {
      case 1:
        retval.maxu = std::numeric_limits<uint8>::max();
        break;

      case 2:
        retval.maxu = std::numeric_limits<uint16>::max();
        break;

      case 4:
        retval.maxu = std::numeric_limits<uint32>::max();
        break;

      case 8:
        retval.maxu = std::numeric_limits<uint64>::max();
        break;
      }
      break;
    }
    case REFType::FloatingPoint: {
      switch (typeSize) {
      case 4:
        retval.maxf = std::numeric_limits<float>::max();
        break;

      case 8:
        retval.maxd = std::numeric_limits<double>::max();
        break;
      }
      break;
    }
    }
  }

  if (!retval.min) {
    switch (type) {
    case REFType::Integer: {
      switch (typeSize) {
      case 1:
        retval.mins = std::numeric_limits<int8>::min();
        break;

      case 2:
        retval.mins = std::numeric_limits<int16>::min();
        break;

      case 4:
        retval.mins = std::numeric_limits<int32>::min();
        break;

      case 8:
        retval.mins = std::numeric_limits<int64>::min();
        break;
      }
      break;
    }
    case REFType::UnsignedInteger: {
      switch (typeSize) {
      case 1:
        retval.minu = std::numeric_limits<uint8>::min();
        break;

      case 2:
        retval.minu = std::numeric_limits<uint16>::min();
        break;

      case 4:
        retval.minu = std::numeric_limits<uint32>::min();
        break;

      case 8:
        retval.minu = std::numeric_limits<uint64>::min();
        break;
      }
      break;
    }
    case REFType::FloatingPoint: {
      switch (typeSize) {
      case 4:
        retval.minf = std::numeric_limits<float>::min();
        break;

      case 8:
        retval.mind = std::numeric_limits<double>::min();
        break;
      }
      break;
    }
    }
  }

  return retval;
}

using SettingsStack = std::vector<std::function<void()>>;

struct AppHelpContextImpl : AppHelpContext {
  std::map<std::string, std::stringstream> tagBuffers;

  std::ostream &GetStream(const std::string &tag) override {
    return tagBuffers[tag] = std::stringstream{};
  }
};

std::string MakeHelp(APPContext &ctx) {
  AppHelpContextImpl helpCtx;
  if (ctx.AdditionalHelp) {
    ctx.AdditionalHelp(&helpCtx, 0);
  }

  if (helpCtx.tagBuffers.empty()) {
    return {};
  }

  std::string retVal;

  for (auto &[tag, data] : helpCtx.tagBuffers) {
    retVal.append(tag).append(":\n");
    retVal.append(std::move(data).str());
  }

  return retVal;
}

void MakeSettingsStack(SettingsStack &stack, ReflectorFriend &reflected) {
  auto rtInstance = RTInstance(reflected);
  auto rtti = rtInstance.Refl();
  auto instance = static_cast<char *>(rtInstance.Instance());
  const size_t numValues = rtti->nTypes;

  for (size_t r = 0; r < numValues; ++r) {
    auto &type = rtti->types[r];
    std::string typeName = TransformTypeName(rtti->typeNames[r]);
    void *addr = instance + type.offset;
    Options typeOpts;

    if (rtti->typeDescs && rtti->typeDescs[r].part2) {
      typeOpts = GetOptions(rtti->typeDescs[r].part2, type.type, type.size);
    } else {
      typeOpts = GetOptions({}, type.type, type.size);
    }

    switch (type.type) {
    case REFType::Bool:
      stack.emplace_back([=]() {
        ImGui::Checkbox(typeName.c_str(), static_cast<bool *>(addr));
      });
      break;
    case REFType::Integer:
    case REFType::FloatingPoint:
    case REFType::UnsignedInteger:
      if (typeOpts.max) {
        stack.emplace_back([=] {
          ImGui::SliderScalar(typeName.c_str(), typeOpts.dataType, addr,
                              &typeOpts.mins, &typeOpts.maxu, typeOpts.format);
        });
      } else {
        stack.emplace_back([=] {
          ImGui::DragScalar(typeName.c_str(), typeOpts.dataType, addr, 1.f,
                            &typeOpts.mins, &typeOpts.maxu, typeOpts.format);
        });
      }
      break;

    case REFType::String: {
      auto str = static_cast<std::string *>(addr);
      std::string dlgId;
      if (typeOpts.filePath) {
        typeName.insert(typeName.begin(), 2, '#');
        dlgId = typeName + "Dlg";
      }

      stack.emplace_back([=] {
        if (typeOpts.filePath) {
          ImGui::TextUnformatted(typeName.c_str() + 2);
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;

        if (typeOpts.hidden) {
          if (ImGui::Button(typeOpts.hiddenState & 1 ? ICON_FA_EYE_SLASH
                                                     : ICON_FA_EYE)) {
            typeOpts.hiddenState++;
          }

          if (!(typeOpts.hiddenState & 1)) {
            flags |= ImGuiInputTextFlags_Password;
          }

          ImGui::SameLine();
        }

        ImGui::InputText(
            typeName.c_str(), str->data(), str->capacity(), flags,
            [](ImGuiInputTextCallbackData *data) {
              auto str = static_cast<std::string *>(data->UserData);
              if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                if (data->BufSize > str->capacity()) {
                  str->reserve(data->BufSize);
                  data->Buf = str->data();
                }

                str->resize(data->BufTextLen);
              }
              return 0;
            },
            str);

        if (typeOpts.filePath) {
          ImGui::SameLine();
          if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
            ImGuiFileDialog::Instance()->OpenDialog(
                dlgId.c_str(),
                typeOpts.folder ? "Choose Folder" : "Choose File",
                typeOpts.fileFilter.empty() ? nullptr
                                            : typeOpts.fileFilter.c_str(),
                *str);
          }

          if (ImGuiFileDialog::Instance()->Display(
                  dlgId.c_str(),
                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking,
                  {512, 256})) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
              *str = ImGuiFileDialog::Instance()->GetFilePathName();
            }
            ImGuiFileDialog::Instance()->Close();
          }
        }
      });
      break;
    }

    case REFType::Class: {
      stack.emplace_back([=] {
        ImGui::TextUnformatted(typeName.c_str());
        ImGui::Separator();
        ImGui::Indent();
      });

      auto refClass =
          reflectorStatic::Registry().at(JenHash(type.asClass.typeHash));
      ReflectedInstance inst(refClass, addr);
      ReflectorPureWrap refWrap(inst);
      MakeSettingsStack(stack, reinterpret_cast<ReflectorFriend &>(refWrap));

      stack.emplace_back([=] { ImGui::Unindent(); });
      break;
    }

    case REFType::Enum: {
      auto refEnum =
          ReflectedEnum::Registry().at(JenHash(type.asClass.typeHash));
      stack.emplace_back([=] {
        if (ImGui::Combo(typeName.c_str(), &typeOpts.selected, refEnum->names,
                         refEnum->numMembers)) {
          switch (type.size) {
          case 1:
            *static_cast<uint8 *>(addr) = refEnum->values[typeOpts.selected];
            break;
          case 2:
            *static_cast<uint16 *>(addr) = refEnum->values[typeOpts.selected];
            break;
          case 4:
            *static_cast<uint32 *>(addr) = refEnum->values[typeOpts.selected];
            break;
          case 8:
            *static_cast<uint64 *>(addr) = refEnum->values[typeOpts.selected];
            break;
          }
        }
      });
      break;
    }
    default:
      throw std::runtime_error("Unhandled reflection type");
    }

    if (rtti->typeDescs && rtti->typeDescs[r].part1) {
      stack.emplace_back([descr = rtti->typeDescs[r].part1, typeName] {
        if (ImGui::GetHoveredID() ==
            ImGui::GetID(typeName.data(), typeName.data() + typeName.size())) {
          ImGui::SetTooltip("%s", descr);
        }
      });
    }
  }
}

void Draw(SettingsStack &stack) {
  for (auto &f : stack) {
    f();
  }
}

struct ModulesContextImpl : ModulesContext {
  std::vector<ModuleInfo> modules;
  int selectedModule = 0;
  bool refreshProcessing = false;
  std::future<std::vector<ModuleInfo>> refreshFuture;
  std::string appFolder;
  std::string appName;
  APPContext moduleCtx{};
  SettingsStack mainSettingsStack;
  SettingsStack moduleSettingsStack;
  std::string helpText;
  std::optional<std::future<void>> processingJob;

  void Refresh() {
    es::Dispose(moduleCtx);
    refreshProcessing = true;
    refreshFuture =
        std::async(std::launch::async, ScanModules,
                   "/home/lukas/github/reviltoolset" /*appFolder*/, appName);
  }
};
} // namespace

namespace ImGui {
// https://github.com/ocornut/imgui/issues/1901
bool Spinner(const char *label, float radius, int thickness, const ImU32 &color,
             float rotationOffset) {
  ImGuiWindow *window = GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(label);

  ImVec2 pos = window->DC.CursorPos;
  ImVec2 size((radius)*2, (radius + style.FramePadding.y) * 2);

  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ItemSize(bb, style.FramePadding.y);
  if (!ItemAdd(bb, id))
    return false;

  // Render
  window->DrawList->PathClear();

  int num_segments = 30;
  float offsetTime = g.Time + rotationOffset;
  int start = abs(ImSin(offsetTime * 1.8f) * (num_segments - 5));

  const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
  const float a_max =
      IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

  const ImVec2 centre =
      ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

  for (int i = 0; i < num_segments; i++) {
    const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
    window->DrawList->PathLineTo(
        ImVec2(centre.x + ImCos(a + offsetTime * 8) * radius,
               centre.y + ImSin(a + offsetTime * 8) * radius));
  }

  window->DrawList->PathStroke(color, false, thickness);
  return true;
}
} // namespace ImGui

void Modules(ModulesContext &ctx_, std::vector<Queue> &queue) {
  ModulesContextImpl &ctx = static_cast<ModulesContextImpl &>(ctx_);
  auto ReloadModule = [&] {
    if (ctx.modules.empty()) {
      return;
    }
    auto &modInfo = ctx.modules.at(ctx.selectedModule);
    ctx.moduleCtx = APPContext(modInfo.module.data(), modInfo.folder, {});
    es::Dispose(ctx.moduleSettingsStack);
    ctx.helpText = MakeHelp(ctx.moduleCtx);
    if (ctx.moduleCtx.info && ctx.moduleCtx.info->settings) {
      MakeSettingsStack(ctx.moduleSettingsStack, *ctx.moduleCtx.info->settings);
    }
  };

  if (ctx.refreshProcessing) {
    if (ctx.refreshFuture.wait_for(std::chrono ::microseconds(10)) ==
            std::future_status::ready &&
        ctx.refreshFuture.valid()) {
      ctx.modules = ctx.refreshFuture.get();
      ctx.refreshProcessing = false;
      ReloadModule();
    }
  }

  if (!ImGui::Begin("Modules", nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return;
  }

  if (ctx.processingJob) {
    if (UIStack(ctx.processingJob->wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready)) {
      ctx.processingJob->get();
      ctx.processingJob.reset();
      ModifyElements([&](ElementAPI &api) { api.Clean(); });
    }
  }

  ImGui::BeginTable("ModulesTbl", 1, ImGuiTableFlags_NoSavedSettings);
  ImGui::TableNextColumn();

  if (ImGui::BeginChild("ModulesTblCommon", {0, -24})) {
    ImGui::TextUnformatted("Common settings");
    Draw(ctx.mainSettingsStack);
    ImGui::Separator();

    if (ImGui::Combo(
            "##ModulesCombo", &ctx.selectedModule,
            [](void *iterPtr, int index, const char **outText) {
              auto iter = static_cast<decltype(ctx.modules) *>(iterPtr);
              *outText = iter->at(index).descrVersion.data();
              return true;
            },
            &ctx.modules, ctx.modules.size())) {
      ReloadModule();
    }

    ImGui::SameLine();
    if (ctx.refreshProcessing) {
      ImGui::Spinner("ModuleRefreshSpin", 8, 2,
                     ImGui::GetColorU32(ImGuiCol_FrameBgActive));
    } else {
      if (ImGui::Button(ICON_FA_REFRESH)) {
        ctx.Refresh();
      }
    }

    if (ctx.moduleCtx.info && ctx.moduleCtx.info->settings) {
      ImGui::TextUnformatted("Module settings");
      Draw(ctx.moduleSettingsStack);
    }

    if (!ctx.helpText.empty()) {
      ImGui::PushStyleColor(ImGuiCol_FrameBg,
                            ImGui::GetColorU32(ImGuiCol_WindowBg));
      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImGui::InputTextMultiline(
          "##module help", ctx.helpText.data(), ctx.helpText.size(),
          {avail.x, std::max(avail.y, 200.f)}, ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();

  ImGui::TableNextColumn();

  if (ImGui::BeginChild("ModulesTblButtons")) {
    bool queueMode =
        ctx.moduleCtx.info && !ctx.moduleCtx.info->batchControlFilters.empty();
    ImGui::BeginDisabled(queueMode);
    if (ImGui::Button("Process current queue")) {
      ctx.processingJob = std::async(
          [payload = MakeWorkerContext(&ctx.moduleCtx), queue = queue] {
            payload->queue = std::move(queue);
            try {
              payload->ProcessQueue();
            } catch (const std::exception &e) {
              printerror(e.what());
            } catch (...) {
              printerror("Uncaught exception");
            }
          });
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!queueMode);
    ImGui::SameLine();
    ImGui::Button("Process current batch");
    ImGui::EndDisabled();
  }
  ImGui::EndChild();

  ImGui::EndTable();

  ImGui::End();
}

std::unique_ptr<ModulesContext> CreateModulesContext(std::string appPath) {
  AFileInfo appLocation(std::move(appPath));

  auto retVal = std::make_unique<ModulesContextImpl>();
  MakeSettingsStack(retVal->mainSettingsStack, MainSettings());
  MakeSettingsStack(retVal->mainSettingsStack, CliSettings());
  retVal->appFolder = appLocation.GetFolder();
  retVal->appName = appLocation.GetFilename();
  retVal->Refresh();

  return retVal;
}

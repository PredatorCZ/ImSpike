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

#include <GL/glew.h>

#include <GLFW/glfw3.h>

#include "ImGuiFileDialog.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "datas/directory_scanner.hpp"
#include "datas/fileinfo.hpp"
#include "datas/master_printer.hpp"
#include "datas/pugiex.hpp"
#include "datas/tchar.hpp"
#include "settings.hpp"

#include "spike/context.hpp"
#include "spike/tmp_storage.hpp"

#include "font_awesome4/definitions.h"
#include "main.hpp"
#include "project.h"

namespace ImGui {
bool Link(const char *label, ImGuiButtonFlags flags = 0) {
  ImGuiWindow *window = GetCurrentWindow();

  if (window->SkipItems) {
    return false;
  }

  const ImVec2 labelSize = CalcTextSize(label, nullptr, true);
  const ImVec2 cursorPos = window->DC.CursorPos;
  const ImRect bb(cursorPos, cursorPos + labelSize);
  const ImGuiID id = window->GetID(label);

  if (!ItemAdd(bb, id)) {
    return false;
  }

  bool hovered;
  bool pressed = ButtonBehavior(bb, id, &hovered, nullptr, flags);

  const ImColor textColor = hovered ? GetColorU32(ImGuiCol_ButtonHovered)
                                    : GetColorU32(ImGuiCol_ButtonActive);
  TextColored(textColor, "%s", label);
  GetWindowDrawList()->AddLine(ImVec2(cursorPos.x, cursorPos.y + labelSize.y),
                               bb.Max, textColor);

  return pressed;
}

void TextCentered(const char *text) {
  auto windowWidth = ImGui::GetWindowWidth();
  auto textWidth = ImGui::CalcTextSize(text).x;

  ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
  ImGui::TextUnformatted(text, ImGui::FindRenderedTextEnd(text));
}

} // namespace ImGui

void WarmColors() {
  ImVec4 *colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = ImVec4(1.f, 1.f, 0.95f, 1.f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
  colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.29f, 0.29f, 0.29f, 0.54f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.53f, 0.53f, 0.53f, 0.40f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.64f, 0.64f, 0.64f, 0.67f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.15f, 0.12f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.02f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.49f, 0.58f, 0.69f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.67f, 0.73f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.70f, 0.70f, 0.70f, 0.40f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.29f, 0.40f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(1.00f, 0.99f, 0.92f, 0.31f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
  colors[ImGuiCol_Tab] = ImVec4(0.43f, 0.40f, 0.31f, 0.86f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.33f, 0.33f, 0.18f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.66f, 0.63f, 0.19f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.33f, 0.36f, 0.12f, 1.00f);
  colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
  colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
  colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

int _tmain(int argc, TCHAR *argv[]) {
  InitLogs();
  es::print::AddPrinterFunction(es::Print);
  CleanTempStorages();
  InitTempStorage();
  auto modules = CreateModulesContext(std::to_string(argv[0]));

  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  pugi::xml_document settingsDoc;
  try {
    settingsDoc = XMLFromFile("settings.conf");
  } catch (const es::FileNotFoundError &) {
  }

  GLFWState gstate;
  LoadSettings(gstate, settingsDoc);

  GLFWwindow *window = glfwCreateWindow(gstate.width, gstate.height,
                                        ImSpike_PRODUCT_NAME, nullptr, nullptr);

  if (!window) {
    glfwTerminate();
    return 1;
  }

  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_CONTEXT_RELEASE_BEHAVIOR, GLFW_RELEASE_BEHAVIOR_FLUSH);

  glfwMakeContextCurrent(window);

  GLenum err = glewInit();

  if (GLEW_OK != err) {
    glfwTerminate();
    return 2;
  }

  ImGuiContext *g = ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  WarmColors();
  ImGui::GetStyle().FrameRounding = 3;
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init();

  io.Fonts->AddFontDefault();

  // merge in icons from Font Awesome
  static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  io.Fonts->AddFontFromFileTTF("font_awesome4/font.ttf", 13, &icons_config,
                               icons_ranges);
  io.FontDefault = io.Fonts->AddFontFromFileTTF("aqum2/aqum-two-classic.otf", 13);

  LoadSettings(*g, settingsDoc);

  auto mounts = CreateMountsContext();

  std::vector<Queue> queue;
  bool openedAbout = false;

  while (!glfwWindowShouldClose(window)) {
    if (g->IO.WantSaveIniSettings) {
      glfwGetWindowSize(window, &gstate.width, &gstate.height);
      SaveSettings(gstate, *g, settingsDoc);
      XMLToFile("settings.conf", settingsDoc);
      g->IO.WantSaveIniSettings = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("EditorWindow", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    if (openedAbout && !ImGui::IsPopupOpen("About##popup")) {
      ImGui::OpenPopup("About##popup");
    }

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("About##popup", &openedAbout,
                               ImGuiWindowFlags_NoResize)) {
      if (ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        ImGui::CloseCurrentPopup();
        openedAbout = false;
      }

      ImGui::BeginTabBar("About Tabs");

      if (ImGui::BeginTabItem("Info")) {
        ImGui::TextCentered(ImSpike_PRODUCT_NAME " V" ImSpike_VERSION);
        ImGui::TextCentered(ImSpike_COPYRIGHT "Lukas Cone");
        ImGui::TextCentered(ImSpike_PRODUCT_NAME
                            " is licensed under GNU GPL v3");
        ImGui::TextCentered("Github repository:##" ImSpike_PRODUCT_NAME);
        ImGui::SameLine();
        if (ImGui::Link(ImSpike_PRODUCT_NAME)) {
          OpenInBrowser("https://github.com/PredatorCZ/ImSpike");
        }
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Modules")) {
        ModuleInfos(*modules);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();

      ImGui::EndPopup();
    }

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) {
          openedAbout = true;
        }
        ImGui::EndMenu();
      }
    }
    ImGui::EndMenuBar();

    ImGuiID dockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockId, {}, ImGuiDockNodeFlags_NoWindowMenuButton);
    static auto first_time = true;
    if (first_time) {
      first_time = false;

      ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetWindowSize());
      auto bottomDockNode = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down,
                                                        0.3f, nullptr, &dockId);

      auto topLeftDockNode = ImGui::DockBuilderSplitNode(
          dockId, ImGuiDir_Left, 0.25f, nullptr, &dockId);

      ImGui::DockBuilderDockWindow("Modules", topLeftDockNode);
      ImGui::DockBuilderDockWindow("File Explorer", dockId);

      auto leftBottomDockNode = ImGui::DockBuilderSplitNode(
          bottomDockNode, ImGuiDir_Left, 0.20f, nullptr, &bottomDockNode);
      auto rightBottomDockNode = ImGui::DockBuilderSplitNode(
          bottomDockNode, ImGuiDir_Right, 0.7f, nullptr, &bottomDockNode);

      ImGui::DockBuilderDockWindow("Queue", bottomDockNode);
      ImGui::DockBuilderDockWindow("Logs", rightBottomDockNode);
      ImGui::DockBuilderDockWindow("Mounts", leftBottomDockNode);
      ImGui::DockBuilderFinish(dockId);
    }
    ImGui::End();

    if (ImGui::Begin("Queue", nullptr, ImGuiWindowFlags_NoCollapse)) {
      if (ImGui::Button("Remove selected items")) {
        std::vector<Queue> newQueue(std::move(queue));

        for (auto &q : newQueue) {
          if (!q.selected) {
            queue.emplace_back(std::move(q));
          }
        }
      }

      ImGui::SameLine();

      if (ImGui::Button("Remove all items")) {
        es::Dispose(queue);
      }

      if (ImGui::BeginTable("QueueTable", 1, ImGuiTableFlags_Borders)) {
        for (auto &q : queue) {
          ImGui::TableNextColumn();
          ImGui::Selectable(q.path1.data(), &q.selected);
        }
      }
      ImGui::EndTable();
    }

    ImGui::End();

    Modules(*modules, queue);
    ExplorerWindow(*mounts, queue);
    MountsWindow(*mounts);
    Logs();

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  CleanCurrentTempStorage();
}

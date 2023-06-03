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

  GLFWwindow *window = glfwCreateWindow(gstate.width, gstate.height, "ImSpike",
                                        nullptr, nullptr);

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
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init();

  io.Fonts->AddFontDefault();

  // merge in icons from Font Awesome
  static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  io.Fonts->AddFontFromFileTTF("imgui/font_awesome4/font.ttf", 13,
                               &icons_config, icons_ranges);

  LoadSettings(*g, settingsDoc);

  auto mounts = CreateMountsContext();

  std::vector<Queue> queue;

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

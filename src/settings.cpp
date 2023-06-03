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

#include "settings.hpp"
#include "datas/pugiex.hpp"
#include "imgui_internal.h"
#include <map>

// COPIED INTERNAL FUNCTIONS

// Clear and initialize empty settings instance
static void TableSettingsInit(ImGuiTableSettings *settings, ImGuiID id,
                              int columns_count, int columns_count_max) {
  IM_PLACEMENT_NEW(settings) ImGuiTableSettings();
  ImGuiTableColumnSettings *settings_column = settings->GetColumnSettings();
  for (int n = 0; n < columns_count_max; n++, settings_column++)
    IM_PLACEMENT_NEW(settings_column) ImGuiTableColumnSettings();
  settings->ID = id;
  settings->ColumnsCount = (ImGuiTableColumnIdx)columns_count;
  settings->ColumnsCountMax = (ImGuiTableColumnIdx)columns_count_max;
  settings->WantApply = true;
}

// Persistent Settings data, stored contiguously in SettingsNodes (sizeof() ~32
// bytes)
struct ImGuiDockNodeSettings {
  ImGuiID ID;
  ImGuiID ParentNodeId;
  ImGuiID ParentWindowId;
  ImGuiID SelectedTabId;
  signed char SplitAxis;
  char Depth;
  ImGuiDockNodeFlags Flags; // NB: We save individual flags one by one in ascii
                            // format (ImGuiDockNodeFlags_SavedFlagsMask_)
  ImVec2ih Pos;
  ImVec2ih Size;
  ImVec2ih SizeRef;
  ImGuiDockNodeSettings() {
    memset(this, 0, sizeof(*this));
    SplitAxis = ImGuiAxis_None;
  }
};

static ImGuiDockNodeSettings *DockSettingsFindNodeSettings(ImGuiContext *ctx,
                                                           ImGuiID id) {
  // FIXME-OPT
  ImGuiDockContext *dc = &ctx->DockContext;
  for (int n = 0; n < dc->NodesSettings.Size; n++)
    if (dc->NodesSettings[n].ID == id)
      return &dc->NodesSettings[n];
  return NULL;
}

namespace ImGui {
const ImGuiID IMGUI_VIEWPORT_DEFAULT_ID = 0x11111111;
}

static void DockSettingsHandler_DockNodeToSettings(ImGuiDockContext *dc,
                                                   ImGuiDockNode *node,
                                                   int depth) {
  ImGuiDockNodeSettings node_settings;
  IM_ASSERT(depth < (1 << (sizeof(node_settings.Depth) << 3)));
  node_settings.ID = node->ID;
  node_settings.ParentNodeId = node->ParentNode ? node->ParentNode->ID : 0;
  node_settings.ParentWindowId = (node->IsDockSpace() && node->HostWindow &&
                                  node->HostWindow->ParentWindow)
                                     ? node->HostWindow->ParentWindow->ID
                                     : 0;
  node_settings.SelectedTabId = node->SelectedTabId;
  node_settings.SplitAxis =
      (signed char)(node->IsSplitNode() ? node->SplitAxis : ImGuiAxis_None);
  node_settings.Depth = (char)depth;
  node_settings.Flags = (node->LocalFlags & ImGuiDockNodeFlags_SavedFlagsMask_);
  node_settings.Pos = ImVec2ih(node->Pos);
  node_settings.Size = ImVec2ih(node->Size);
  node_settings.SizeRef = ImVec2ih(node->SizeRef);
  dc->NodesSettings.push_back(node_settings);
  if (node->ChildNodes[0])
    DockSettingsHandler_DockNodeToSettings(dc, node->ChildNodes[0], depth + 1);
  if (node->ChildNodes[1])
    DockSettingsHandler_DockNodeToSettings(dc, node->ChildNodes[1], depth + 1);
}

// COPIED INTERNAL FUNCTIONS END

void LoadWindow(pugi::xml_node w, ImGuiSettingsHandler *entry_handler,
                ImGuiContext &g) {
  auto windowName = w.attribute("name");
  ImGuiWindowSettings *settings =
      (ImGuiWindowSettings *)entry_handler->ReadOpenFn(&g, entry_handler,
                                                       windowName.as_string());

  if (!settings) {
    return;
  }

  if (auto attr = w.attribute("PosX")) {
    settings->Pos.x = attr.as_int();
  }

  if (auto attr = w.attribute("PosY")) {
    settings->Pos.y = attr.as_int();
  }

  if (auto attr = w.attribute("SizeX")) {
    settings->Size.x = attr.as_int();
  }

  if (auto attr = w.attribute("SizeY")) {
    settings->Size.y = attr.as_int();
  }

  if (auto attr = w.attribute("ViewportId")) {
    settings->ViewportId = attr.as_ullong();
  }

  if (auto attr = w.attribute("ViewportPosX")) {
    settings->ViewportPos.x = attr.as_int();
  }

  if (auto attr = w.attribute("ViewportPosY")) {
    settings->ViewportPos.y = attr.as_int();
  }

  if (auto attr = w.attribute("Collapsed")) {
    settings->Collapsed = attr.as_bool();
  }

  if (auto attr = w.attribute("DockId")) {
    settings->DockId = attr.as_ullong();
  }

  if (auto attr = w.attribute("DockOrder")) {
    settings->DockOrder = attr.as_int();
  }

  if (auto attr = w.attribute("ClassId")) {
    settings->ClassId = attr.as_ullong();
  }
}

void LoadTable(pugi::xml_node w, ImGuiSettingsHandler *, ImGuiContext &g) {
  ImGuiID tableId = w.attribute("ID").as_ullong();
  ImGuiTableSettings *settings = ImGui::TableSettingsFindByID(tableId);
  uint32 numColumns = w.attribute("ColumnsCount").as_uint();

  if (settings) {
    if (settings->ColumnsCountMax >= numColumns) {
      TableSettingsInit(settings, tableId, numColumns,
                        settings->ColumnsCountMax); // Recycle
    } else {
      // Invalidate storage, we won't fit because of a count change
      settings->ID = 0;
      settings = ImGui::TableSettingsCreate(tableId, numColumns);
    }
  } else {
    settings = ImGui::TableSettingsCreate(tableId, numColumns);
  }

  if (!settings) {
    return;
  }

  if (auto attr = w.attribute("RefScale")) {
    settings->RefScale = attr.as_float();
  }

  for (auto col : w.children()) {
    uint32 index = col.attribute("Index").as_int();

    if (index < 0 || index >= settings->ColumnsCount) {
      continue;
    }

    ImGuiTableColumnSettings *column = settings->GetColumnSettings() + index;
    column->Index = (ImGuiTableColumnIdx)index;

    if (auto attr = col.attribute("UserID")) {
      column->UserID = attr.as_ullong();
    }

    if (auto attr = col.attribute("Width")) {
      column->WidthOrWeight = attr.as_float();
      column->IsStretch = 0;
      settings->SaveFlags |= ImGuiTableFlags_Resizable;
    }

    if (auto attr = col.attribute("Weight")) {
      column->WidthOrWeight = attr.as_float();
      column->IsStretch = 1;
      settings->SaveFlags |= ImGuiTableFlags_Resizable;
    }

    if (auto attr = col.attribute("Visible")) {
      column->IsEnabled = attr.as_bool();
      settings->SaveFlags |= ImGuiTableFlags_Hideable;
    }

    if (auto attr = col.attribute("DisplayOrder")) {
      column->DisplayOrder = attr.as_uint();
      settings->SaveFlags |= ImGuiTableFlags_Reorderable;
    }

    if (auto attr = col.attribute("SortOrder")) {
      column->DisplayOrder = attr.as_uint();
      column->SortDirection = col.attribute("SortDirection").as_uint();
      settings->SaveFlags |= ImGuiTableFlags_Sortable;
    }
  }
}

void LoadDocking(pugi::xml_node w, ImGuiContext &g, bool asDockSpace) {
  ImGuiDockNodeSettings node;

  if (asDockSpace) {
    node.Flags |= ImGuiDockNodeFlags_DockSpace;
  }

  node.ID = w.attribute("ID").as_ullong();
  node.ParentNodeId = w.attribute("ParentNodeId").as_ullong();
  node.ParentWindowId = w.attribute("ParentWindowId").as_ullong();

  if (node.ParentWindowId == 0 && node.ParentWindowId == 0) {
    return;
  }

  if (node.ParentNodeId == 0) {
    node.Pos.x = w.attribute("PosX").as_int();
    node.Pos.y = w.attribute("PosY").as_int();
    node.Size.x = w.attribute("SizeX").as_int();
    node.Size.y = w.attribute("SizeY").as_int();
  } else {
    node.SizeRef.x = w.attribute("SizeRefX").as_int();
    node.SizeRef.y = w.attribute("SizeRefY").as_int();
  }

  if (auto attr = w.attribute("SplitAxis")) {
    node.SplitAxis = attr.as_int();
  }

  if (auto attr = w.attribute("NoResize")) {
    node.Flags |= ImGuiDockNodeFlags_NoResize;
  }

  if (auto attr = w.attribute("CentralNode")) {
    node.Flags |= ImGuiDockNodeFlags_CentralNode;
  }

  if (auto attr = w.attribute("HiddenTabBar")) {
    node.Flags |= ImGuiDockNodeFlags_HiddenTabBar;
  }

  if (auto attr = w.attribute("NoWindowMenuButton")) {
    node.Flags |= ImGuiDockNodeFlags_NoWindowMenuButton;
  }

  if (auto attr = w.attribute("NoCloseButton")) {
    node.Flags |= ImGuiDockNodeFlags_NoCloseButton;
  }

  if (auto attr = w.attribute("SelectedTabId")) {
    node.SelectedTabId = attr.as_ullong();
  }

  if (node.ParentNodeId != 0) {
    if (ImGuiDockNodeSettings *parent_settings =
            DockSettingsFindNodeSettings(&g, node.ParentNodeId)) {
      node.Depth = parent_settings->Depth + 1;
    }
  }
  g.DockContext.NodesSettings.push_back(node);
}

void LoadDockNode(pugi::xml_node w, ImGuiSettingsHandler *, ImGuiContext &g) {
  LoadDocking(w, g, false);
}

void LoadDockSpace(pugi::xml_node w, ImGuiSettingsHandler *, ImGuiContext &g) {
  LoadDocking(w, g, true);
}

static const std::map<std::string_view,
                      void (*)(pugi::xml_node, ImGuiSettingsHandler *,
                               ImGuiContext &)>
    LOADERS{
        {"Window", LoadWindow},
        {"Table", LoadTable},
        {"DockNode", LoadDockNode},
        {"DockSpace", LoadDockSpace},
    };

void LoadSettings(GLFWState &state, pugi::xml_document &doc) {
  auto glfwState = doc.child("glfw_state");
  if (auto attr = glfwState.attribute("Width")) {
    state.width = attr.as_uint();
  }

  if (auto attr = glfwState.attribute("Height")) {
    state.height = attr.as_uint();
  }
}

void LoadSettings(ImGuiContext &g, pugi::xml_document &doc) {
  IM_ASSERT(g.Initialized);
  auto imstate = doc.child("imgui_state");

  // Call pre-read handlers
  // Some types will clear their data (e.g. dock information) some types will
  // allow merge/override (window)
  for (int handler_n = 0; handler_n < g.SettingsHandlers.Size; handler_n++)
    if (g.SettingsHandlers[handler_n].ReadInitFn)
      g.SettingsHandlers[handler_n].ReadInitFn(&g,
                                               &g.SettingsHandlers[handler_n]);

  for (auto state : imstate) {
    ImGuiSettingsHandler *entry_handler =
        ImGui::FindSettingsHandler(state.name());
    if (entry_handler) {
      LOADERS.at(state.name())(state, entry_handler, g);
    }
  }

  g.SettingsLoaded = true;

  // Call post-read handlers
  for (int handler_n = 0; handler_n < g.SettingsHandlers.Size; handler_n++) {
    if (g.SettingsHandlers[handler_n].ApplyAllFn) {
      g.SettingsHandlers[handler_n].ApplyAllFn(&g,
                                               &g.SettingsHandlers[handler_n]);
    }
  }
}

void SaveWindows(ImGuiContext &g, pugi::xml_node state) {
  for (int i = 0; i != g.Windows.Size; i++) {
    ImGuiWindow *window = g.Windows[i];
    if (window->Flags & ImGuiWindowFlags_NoSavedSettings)
      continue;

    ImGuiWindowSettings *settings =
        (window->SettingsOffset != -1)
            ? g.SettingsWindows.ptr_from_offset(window->SettingsOffset)
            : ImGui::FindWindowSettings(window->ID);
    if (!settings) {
      settings = ImGui::CreateNewWindowSettings(window->Name);
      window->SettingsOffset = g.SettingsWindows.offset_from_ptr(settings);
    }
    IM_ASSERT(settings->ID == window->ID);
    settings->Pos = ImVec2ih(window->Pos.x - window->ViewportPos.x,
                             window->Pos.y - window->ViewportPos.y);
    settings->Size = ImVec2ih(window->SizeFull);
    settings->ViewportId = window->ViewportId;
    settings->ViewportPos = ImVec2ih(window->ViewportPos);
    IM_ASSERT(window->DockNode == NULL ||
              window->DockNode->ID == window->DockId);
    settings->DockId = window->DockId;
    settings->ClassId = window->WindowClass.ClassId;
    settings->DockOrder = window->DockOrder;
    settings->Collapsed = window->Collapsed;
  }

  for (ImGuiWindowSettings *settings = g.SettingsWindows.begin();
       settings != NULL; settings = g.SettingsWindows.next_chunk(settings)) {
    auto node = state.append_child("Window");
    node.append_attribute("Name").set_value(settings->GetName());

    if (settings->ViewportId != ImGui::IMGUI_VIEWPORT_DEFAULT_ID) {
      if (settings->ViewportId != 0) {
        node.append_attribute("ViewportId").set_value(settings->ViewportId);
      }

      if (settings->ViewportPos.x != 0) {
        node.append_attribute("ViewportPosX")
            .set_value(settings->ViewportPos.x);
      }

      if (settings->ViewportPos.y != 0) {
        node.append_attribute("ViewportPosY")
            .set_value(settings->ViewportPos.y);
      }
    }

    if (settings->Pos.x != 0) {
      node.append_attribute("PosX").set_value(settings->Pos.x);
    }

    if (settings->Pos.y != 0) {
      node.append_attribute("PosY").set_value(settings->Pos.y);
    }

    if (settings->Size.x != 0) {
      node.append_attribute("SizeX").set_value(settings->Size.x);
    }

    if (settings->Size.y != 0) {
      node.append_attribute("SizeY").set_value(settings->Size.y);
    }

    node.append_attribute("Collapsed").set_value(settings->Collapsed);

    if (settings->DockId != 0) {
      node.append_attribute("DockId").set_value(settings->DockId);
    }

    if (settings->DockOrder != -1) {
      node.append_attribute("DockOrder").set_value(settings->DockOrder);
    }

    if (settings->ClassId != 0) {
      node.append_attribute("ClassId").set_value(settings->ClassId);
    }
  }
}

void SaveTables(ImGuiContext &g, pugi::xml_node state) {
  for (ImGuiTableSettings *settings = g.SettingsTables.begin();
       settings != NULL; settings = g.SettingsTables.next_chunk(settings)) {
    if (settings->ID == 0) // Skip ditched settings
      continue;

    // TableSaveSettings() may clear some of those flags when we establish that
    // the data can be stripped (e.g. Order was unchanged)
    const bool save_size =
        (settings->SaveFlags & ImGuiTableFlags_Resizable) != 0;
    const bool save_visible =
        (settings->SaveFlags & ImGuiTableFlags_Hideable) != 0;
    const bool save_order =
        (settings->SaveFlags & ImGuiTableFlags_Reorderable) != 0;
    const bool save_sort =
        (settings->SaveFlags & ImGuiTableFlags_Sortable) != 0;
    if (!save_size && !save_visible && !save_order && !save_sort)
      continue;

    auto node = state.append_child("Table");
    node.append_attribute("ID").set_value(settings->ID);
    node.append_attribute("ColumnsCount").set_value(settings->ColumnsCount);

    if (settings->RefScale != 0.0f) {
      node.append_attribute("RefScale").set_value(settings->RefScale);
    }

    ImGuiTableColumnSettings *column = settings->GetColumnSettings();

    for (int column_n = 0; column_n < settings->ColumnsCount;
         column_n++, column++) {
      // "Column 0  UserID=0x42AD2D21 Width=100 Visible=1 Order=0 Sort=0v"
      bool save_column = column->UserID != 0 || save_size || save_visible ||
                         save_order || (save_sort && column->SortOrder != -1);
      if (!save_column)
        continue;
      auto columnNode = node.append_child("Column");
      columnNode.append_attribute("Index").set_value(column_n);

      if (column->UserID != 0) {
        columnNode.append_attribute("UserID").set_value(column->UserID);
      }
      if (save_size && column->IsStretch) {
        columnNode.append_attribute("Weight").set_value(column->WidthOrWeight);
      }
      if (save_size && !column->IsStretch) {
        columnNode.append_attribute("Width").set_value(column->WidthOrWeight);
      }
      if (save_visible) {
        columnNode.append_attribute("Visible").set_value(column->IsEnabled);
      }
      if (save_order) {
        columnNode.append_attribute("DisplayOrder")
            .set_value(column->DisplayOrder);
      }
      if (save_sort && column->SortOrder != -1) {
        columnNode.append_attribute("SortOrder").set_value(column->SortOrder);
        columnNode.append_attribute("SortDirection")
            .set_value(column->SortDirection);
      }
    }
  }
}
void SaveDocking(ImGuiContext &g, pugi::xml_node state) {
  ImGuiDockContext &dc = g.DockContext;
  if (!(g.IO.ConfigFlags & ImGuiConfigFlags_DockingEnable))
    return;

  // Gather settings data
  // (unlike our windows settings, because nodes are always built we can do a
  // full rewrite of the SettingsNode buffer)
  dc.NodesSettings.resize(0);
  dc.NodesSettings.reserve(dc.Nodes.Data.Size);
  for (int n = 0; n < dc.Nodes.Data.Size; n++) {
    if (ImGuiDockNode *node = (ImGuiDockNode *)dc.Nodes.Data[n].val_p) {
      if (node->IsRootNode()) {
        DockSettingsHandler_DockNodeToSettings(&dc, node, 0);
      }
    }
  }

  int max_depth = 0;
  for (int node_n = 0; node_n < dc.NodesSettings.Size; node_n++)
    max_depth = ImMax((int)dc.NodesSettings[node_n].Depth, max_depth);

  for (int node_n = 0; node_n < dc.NodesSettings.Size; node_n++) {
    const ImGuiDockNodeSettings *node_settings = &dc.NodesSettings[node_n];
    const char *nodeName = "DockNode";
    if (node_settings->Flags & ImGuiDockNodeFlags_DockSpace) {
      nodeName = "DockSpace";
    }

    auto node = state.append_child(nodeName);
    node.append_attribute("Depth").set_value(node_settings->Depth);
    node.append_attribute("ID").set_value(node_settings->ID);

    if (node_settings->ParentNodeId) {
      node.append_attribute("ParentNodeId")
          .set_value(node_settings->ParentNodeId);
      node.append_attribute("SizeRefX").set_value(node_settings->SizeRef.x);
      node.append_attribute("SizeRefY").set_value(node_settings->SizeRef.y);
    } else {
      if (node_settings->ParentWindowId) {
        node.append_attribute("ParentWindowId")
            .set_value(node_settings->ParentWindowId);
      }

      node.append_attribute("PosX").set_value(node_settings->Pos.x);
      node.append_attribute("PosY").set_value(node_settings->Pos.y);
      node.append_attribute("SizeX").set_value(node_settings->Size.x);
      node.append_attribute("SizeY").set_value(node_settings->Size.y);
    }

    if (node_settings->SplitAxis != ImGuiAxis_None) {
      node.append_attribute("SplitAxis").set_value(node_settings->SplitAxis);
    }

    if (node_settings->Flags & ImGuiDockNodeFlags_NoResize) {
      node.append_attribute("NoResize");
    }
    if (node_settings->Flags & ImGuiDockNodeFlags_CentralNode) {
      node.append_attribute("CentralNode");
    }
    if (node_settings->Flags & ImGuiDockNodeFlags_NoTabBar) {
      node.append_attribute("NoTabBar");
    }
    if (node_settings->Flags & ImGuiDockNodeFlags_HiddenTabBar) {
      node.append_attribute("HiddenTabBar");
    }
    if (node_settings->Flags & ImGuiDockNodeFlags_NoWindowMenuButton) {
      node.append_attribute("NoWindowMenuButton");
    }
    if (node_settings->Flags & ImGuiDockNodeFlags_NoCloseButton) {
      node.append_attribute("NoCloseButton");
    }

    if (node_settings->SelectedTabId) {
      node.append_attribute("SelectedTabId")
          .set_value(node_settings->SelectedTabId);
    }
  }
}

void SaveSettings(GLFWState &state, ImGuiContext &g, pugi::xml_document &doc) {
  g.SettingsDirtyTimer = 0.0f;
  doc.remove_child("glfw_state");
  auto glfw = doc.append_child("glfw_state");
  glfw.append_attribute("Width").set_value(state.width);
  glfw.append_attribute("Height").set_value(state.height);

  doc.remove_child("imgui_state");
  auto state_ = doc.append_child("imgui_state");
  SaveWindows(g, state_);
  SaveTables(g, state_);
  SaveDocking(g, state_);
}

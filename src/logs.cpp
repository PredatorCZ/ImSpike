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

#include "datas/master_printer.hpp"
#include "font_awesome4/definitions.h"
#include "imgui.h"
#include <algorithm>
#include <atomic>
#include <vector>

static std::vector<es::print::Queuer> logLines;
static std::vector<es::print::Queuer> messageQueues[2];
static std::atomic<bool> messageQueueOrder;

void ReceiveQueue(const es::print::Queuer &que) {
  messageQueues[messageQueueOrder].push_back(que);
}

void InitLogs() { es::print::AddQueuer(ReceiveQueue); }

void Logs() {
  if (bool mqo = messageQueueOrder; !messageQueues[mqo].empty()) {
    messageQueueOrder = !mqo;
    logLines.insert(logLines.end(),
                    std::make_move_iterator(messageQueues[mqo].begin()),
                    std::make_move_iterator(messageQueues[mqo].end()));
    messageQueues[mqo].clear();
  }

  if (ImGui::Begin("Logs", nullptr, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTable("LogsTable", 3,
                          ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_Borders)) {

      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 10);
      ImGui::TableSetupColumn("Text");
      ImGui::TableSetupColumn("Thread ID", ImGuiTableColumnFlags_WidthFixed,
                              60);
      ImGui::TableHeadersRow();

      for (auto &l : logLines) {
        ImGui::TableNextColumn();
        if (l.type > es::print::MPType::MSG) {
          static const char *types[]{nullptr, nullptr,
                                     ICON_FA_EXCLAMATION_TRIANGLE,
                                     ICON_FA_TIMES_CIRCLE, ICON_FA_INFO_CIRCLE};
          static const ImVec4 colors[]{
              ImVec4(1, 1, 1, 1),       ImVec4(1, 1, 1, 1),
              ImVec4(1, 1, 0.125, 1),   ImVec4(1, 0.125, 0.125, 1),
              ImVec4(0.125, 0.5, 1, 1),
          };
          ImGui::TextColored(colors[uint32(l.type)], "%s",
                             types[uint32(l.type)]);
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(l.payload.data(),
                               l.payload.data() + l.payload.size());
        ImGui::TableNextColumn();
        if (l.threadId) {
          ImGui::Text("%X", l.threadId);
        }
        ImGui::TableNextRow();
      }

      ImGui::EndTable();
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1);
      }
    }
  }

  ImGui::End();
}

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

#include "datas/supercore.hpp"
#include "font_awesome4/definitions.h"
#include "imgui.h"
#include "main.hpp"
#include "spike/console.hpp"

void ProgressBar::PrintLine() {
  const float normState = std::min(curitem * itemDelta, 1.f);
  ImGui::TextUnformatted(label.data());

  char percBuffer[16]{};
  snprintf(percBuffer, sizeof(percBuffer), "%3u", uint32(normState * 100));
  ImGui::ProgressBar(normState, {-1, 0}, percBuffer);
}

void DetailedProgressBar::PrintLine() {
  const float normState = std::min(curitem * itemDelta, 1.f);
  ImGui::TextUnformatted(label.data());

  char percBuffer[16]{};
  snprintf(percBuffer, sizeof(percBuffer), "%3u", uint32(normState * 100));
  ImGui::ProgressBar(normState, {-1, 0}, percBuffer);
}

void LoadingBar::PrintLine() {
  if (!state) {
    ImGui::Spinner(payload.data(), 4, 3,
                   ImGui::GetColorU32(ImGuiCol_FrameBgActive),
                   payload.size() - 1.f);
  } else if (state == 1) {
    ImGui::TextColored({0, 1, 0, 1}, "%s", ICON_FA_CHECK);
  } else {
    ImGui::TextColored({1, 0, 0, 1}, "%s", ICON_FA_TIMES);
  }

  ImGui::SameLine();
  ImGui::TextUnformatted(payload.data());
}

static std::atomic_uint8_t queueIndex{0};
static std::vector<std::shared_ptr<LogLine>> lineQueue[2];

bool UIStack(bool isDone) {
  bool readyClose = false;

  if (!ImGui::IsPopupOpen("##UISTACK")) {
    ImGui::OpenPopup("##UISTACK");
  }

  if (!ImGui::BeginPopupModal("##UISTACK")) {
    return false;
  }

  const uint8 queIndex = queueIndex;

  for (auto &line : lineQueue[queIndex]) {
    line->PrintLine();
  }

  if (isDone) {
    if (ImGui::Button("Close")) {
      readyClose = true;
    }
  }

  ImGui::EndPopup();

  return readyClose;
}

static ElementAPI EAPI;

void ElementAPI::Append(std::unique_ptr<LogLine> &&item) {
  lineQueue[!queueIndex].emplace_back(std::move(item));
}

void ElementAPI::Remove(LogLine *item) {}

void ElementAPI::Release(LogLine *line) {}

void ElementAPI::Clean() {
  for (auto &q : lineQueue) {
    q.clear();
  }
}

void ElementAPI::Insert(std::unique_ptr<LogLine> &&item, LogLine *where,
                        bool after) {
  auto &curQue = lineQueue[!queueIndex];
  auto found = std::find_if(curQue.begin(), curQue.end(),
                            [&](auto &item_) { return item_.get() == where; });
  curQue.insert(std::next(found, after), std::move(item));
}

void ModifyElements_(element_callback cb) {
  cb(EAPI);
  queueIndex = !queueIndex;
  lineQueue[!queueIndex] = lineQueue[queueIndex];
}

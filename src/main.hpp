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

#include <memory>
#include <string>
#include <vector>

using ImU32 = unsigned;
struct APPContext;

struct MountManager {
  virtual void AddMount(std::string path) = 0;
  virtual ~MountManager() = default;
};

struct Queue {
  std::string path0;
  std::string path1;
  bool selected = false;
  bool isFolder = false;
};

struct QueueContext {
  std::vector<Queue> queue;
  virtual void ProcessQueue() = 0;
  virtual ~QueueContext() = default;
};

std::shared_ptr<QueueContext> MakeWorkerContext(APPContext *ctx);

void ExplorerWindow(MountManager &man, std::vector<Queue> &queue);
void MountsWindow(MountManager &man);
std::unique_ptr<MountManager> CreateMountsContext();

void InitLogs();
void Logs();

struct ModulesContext {
  virtual ~ModulesContext() = default;
};

void Modules(ModulesContext &ctx, std::vector<Queue> &queue);
void ModuleInfos(ModulesContext &ctx);
std::unique_ptr<ModulesContext> CreateModulesContext(std::string appPath);

namespace ImGui {
bool Spinner(const char *label, float radius, int thickness, const ImU32 &color,
             float rotationOffset = 0.f);
}

bool UIStack(bool isDone);
void OpenInBrowser(const std::string &url);

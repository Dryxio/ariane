#pragma once

#include <vector>

namespace SAPaths {

struct Node;
struct BackupFile {
	char logicalPath[256];
	char sourcePath[1024];
	std::vector<uint8> data;
};

extern Node *hoveredNode;
extern Node *selectedNode;

void Reset(void);
void EnsureLoaded(void);
bool HasSelectedNode(void);
bool GetSelectedNodePosition(rw::V3d *pos);
bool SetSelectedNodePosition(const rw::V3d &pos, bool notifyChange);
void RenderCarPaths(void);
void RenderPedPaths(void);
void RenderAreaGrid(void);
bool HasInfoToShow(void);
void DrawInfoPanel(void);
int GetDirtyAreaCount(void);
bool HasDirtyAreas(void);
bool SaveDirtyAreas(int *savedAreas = nil);
bool CollectDirtyAreaBackupFiles(std::vector<BackupFile> &files);
void CommitSelectedNodeEdit(void);

}

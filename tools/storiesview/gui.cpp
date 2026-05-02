#include "storiesview.h"

static bool showTimeWeatherWindow;
static bool showViewWindow;
static bool showObjectWindow;
static bool showSwapWindow;

static void
advanceHour(int diff)
{
	currentHour += diff;
	if(currentHour >= 24)
		currentHour = 0;
	else if(currentHour < 0)
		currentHour = 23;
}

static void
advanceMinute(int diff)
{
	currentMinute += diff;
	if(currentMinute >= 60){
		currentMinute = 0;
		advanceHour(1);
	}else if(currentMinute < 0){
		currentMinute = 59;
		advanceHour(-1);
	}
}

static void
uiTimeWeather(void)
{
	ImGui::PushItemWidth(100);
	ImGui::BeginGroup();
	ImGui::Text("Hour");
	ImGui::InputInt("##Hour", &currentHour, 1);
	advanceHour(0);
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::Text("Minute");
	ImGui::InputInt("##Minute", &currentMinute, 1);
	advanceMinute(0);
	ImGui::EndGroup();

	ImGui::PopItemWidth();


	ImGui::InputInt("Weather", &currentWeather, 1);
	if(currentWeather >= 8) currentWeather = -1;
	if(currentWeather < -1) currentWeather = 7;

	ImGui::InputInt("Interior", &currentInterior, 1);
	if(currentInterior >= gLevel->chunk->numInteriors) currentInterior = -1;
	if(currentInterior < -1) currentInterior = gLevel->chunk->numInteriors-1;

#ifndef VCS
	static int swapslot;
	ImGui::PushItemWidth(100);
	ImGui::BeginGroup();
	ImGui::Text("Swap slot");
	ImGui::InputInt("##Slot", &swapslot, 1);
	ImGui::EndGroup();
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::Text("state");
	ImGui::InputInt("##Swapstate", &swapstate[swapslot], 1);
	if(swapstate[swapslot] < 0) swapstate[swapslot] = 0;
	ImGui::EndGroup();
	ImGui::PopItemWidth();
#endif
}

void
updatePassMask(void)
{
	passmask[SECLIST_SUPERLOD] = drawLOD | drawCurrentSector;
	passmask[SECLIST_LOD] = drawLOD | drawCurrentSector;
#ifdef VCS
	passmask[SECLIST_UNDERWATER] = !drawLOD | drawCurrentSector;
#endif
	passmask[SECLIST_ROADS] = !drawLOD | drawCurrentSector;
	passmask[SECLIST_NORMAL] = !drawLOD | drawCurrentSector;
	passmask[SECLIST_NOZWRITE] = !drawLOD | drawCurrentSector;
	passmask[SECLIST_LIGHTS] = !drawLOD | drawCurrentSector;
	passmask[SECLIST_TRANSPARENT] = !drawLOD | drawCurrentSector;
}

static void
uiView(void)
{
	if(ImGui::Checkbox("Render only current Sector", &drawCurrentSector)) updatePassMask();
	if(!drawCurrentSector)
		ImGui::Checkbox("Render all interiors", &drawAllInteriors);

	
//	ImGui::Text("Draw"); ImGui::SameLine();
	ImGui::RadioButton("All", &drawFlagged, 2); ImGui::SameLine();
	ImGui::RadioButton("Flagged", &drawFlagged, 0); ImGui::SameLine();
	ImGui::RadioButton("Unflagged", &drawFlagged, 1);

	ImGui::Checkbox("Draw only time/swap models", &drawOnlySwaps);
	ImGui::Checkbox("Draw all timed models", &ignoreTime);
	ImGui::Checkbox("Draw all swap models", &ignoreSwapState);
//	ImGui::Checkbox("Render Dynamic objects (COL only)", &drawDummies);
	if(ImGui::Checkbox("Render LOD", &drawLOD)) updatePassMask();

	ImGui::NewLine();

	static char *passnames[] = {
		"SuperLOD",
#ifdef VCS
		"Underwater",
#endif
		"LOD",
		"Roads",
		"Normal",
		"NoZWrite",
		"Lights",
		"Transparent"
	};
	for(int i = 0; i < SECLIST_END; i++)
		ImGui::Checkbox(passnames[i], &passmask[i]);

	ImGui::NewLine();

	if(ImGui::Checkbox("Render World", &drawWorld)) updatePassMask();
	ImGui::Checkbox("Render Water", &drawWater);
	ImGui::Checkbox("Render Path nodes", &drawPathNodes);
	ImGui::Checkbox("Render Collision", &drawCol);
	ImGui::Checkbox("Render Solid Bounds", &drawBounds);

	ImGui::NewLine();

	ImGui::Checkbox("Render only unnamed", &drawUnnamed);
	ImGui::Checkbox("Render only Unmatched", &drawUnmatched);
}

static void
entityInfo(CEntity *e)
{
	static char tmp[64];
	int i;

	CBaseModelInfo *mi = CModelInfo::Get(e->modelIndex);
	EntityExt *ee = (EntityExt*)e->vtable;

//	ImGui::Text("model: %d, ", e->modelIndex);
//	ImGui::SameLine();
//	strcpy(tmp, mi->name);
//	ImGui::InputText(".", tmp, 64);
	ImGui::Text("model: %d, %s", e->modelIndex, mi->name);
	if(mi->type == MODELINFO_SIMPLE || mi->type == MODELINFO_TIME){
		CTimeModelInfo *si = (CTimeModelInfo*)mi;
		ImGui::SameLine();
		ImGui::Text(", %d, %f", si->numObjects, si->drawDistances[0]);
		if(mi->type == MODELINFO_TIME){
			ImGui::Text("time: %d %d", si->timeOn, si->timeOff);
		}
	}

	sprintf(tmp, "Entity 0x%X %p", ee->GetIplID(), e);
	ImGui::PushID(e);
	ImGui::Selectable(tmp);
	ImGui::PopID();
	if(ImGui::IsItemHovered()){
		ee->highlight = 1;
		if(ImGui::IsMouseClicked(1))
			ee->Select();
		if(ImGui::IsMouseDoubleClicked(0))
			ee->JumpTo();
	}

	ImGui::Text("Buildings:");
	for(i = 0; i < ee->n; i++){
		sprintf(tmp, "  0x%X %d %s", ee->insts[i]->id, ee->insts[i]->modelId,
			CModelInfo::Get(ee->insts[i]->modelId)->name);
		ImGui::PushID(ee->insts[i]);
		ImGui::Selectable(tmp);
		ImGui::PopID();
		if(ImGui::IsItemHovered()){
			ee->insts[i]->highlight = 1;
			if(ImGui::IsMouseClicked(1))
				ee->insts[i]->Select();
		}
	}
}

static void
listEntity(CEntity *e, ImGuiTextFilter *filter, bool highlight)
{
	EntityExt *ee;
	ee = (EntityExt*)e->vtable;
	CBaseModelInfo *mi = CModelInfo::Get(e->modelIndex);
	if(filter->PassFilter(mi->name)){
		bool pop = false;
		if(ee->selected){
			ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 0, 0));
			pop = true;
		}
		ImGui::PushID(e);
		ImGui::Selectable(mi->name);
		ImGui::PopID();
		if(ImGui::IsItemHovered()){
			if(ImGui::IsMouseClicked(1))
				ee->Select();
			if(ImGui::IsMouseDoubleClicked(0))
				ee->JumpTo();
		}
		if(pop)
			ImGui::PopStyleColor();
		if(highlight)
			ee->highlight = HIGHLIGHT_FILTER;
		if(ImGui::IsItemHovered())
			ee->highlight = HIGHLIGHT_HOVER;
	}
}

static void
uiObject(void)
{
	int i;

	BuildingExt *b = BuildingExt::GetSelection();
	if(b && ImGui::TreeNode("Building")){
		ImGui::Text("0x%X %p", b->id, b);
		ImGui::Separator();
		if(b->iplId >= 0){
			CEntity *e = GetEntityById(b->iplId);
			entityInfo(e);
		}
		ResourceExt *re = &gLevel->res[b->resources->resId];
		sLevelSwap *swap;
		if(b->swap) swap = b->swap;
		else if (re->sector && re->sector->swap) swap = re->sector->swap;
		else swap = nil;
		if(swap){
			if(swap->IsUnconditional())
				ImGui::Text("%d %d", swap->GetTimeOff_Slot(), swap->GetTimeOn_State());
			if(swap->IsTimed())
				ImGui::Text("time %d %d", swap->GetTimeOff_Slot(), swap->GetTimeOn_State());
			else
				ImGui::Text("cond %d %d", swap->GetTimeOff_Slot(), swap->GetTimeOn_State());
		}
		ImGui::TreePop();
	}

	CEntity *e = EntityExt::GetSelection();
	if(e && ImGui::TreeNode("Entity")){
		entityInfo(e);
		ImGui::TreePop();
	}

	ImGui::Separator();

	if(ImGui::TreeNode("Instances")){
		static ImGuiTextFilter filter;
		filter.Draw();
		static bool highlight;
		ImGui::Checkbox("Highlight matches", &highlight);

		int n;
		n = pBuildingPool->GetSize();
		for(i = 0; i < n; i++){
			e = pBuildingPool->GetSlot(i);
			if(e == nil)
				continue;
			listEntity(e, &filter, highlight);
		}
		if(drawDummies){
			n = pDummyPool->GetSize();
			for(i = 0; i < n; i++){
				e = pDummyPool->GetSlot(i);
				if(e == nil)
					continue;
				listEntity(e, &filter, highlight);
			}
		}
		ImGui::TreePop();
	}

	if(ImGui::TreeNode("Building")){
		for(i = 0; i < 0x8000; i++){
			if(gLevel->buildings[i] == nil)
				continue;
			BuildingExt *b = gLevel->buildings[i];
			if(b->iplId < 0 || b->modelId < 0){
				char tmp[100];
				sprintf(tmp, "%X  %X  %d", i, b->iplId, b->modelId);
				ImGui::PushID(i);
				ImGui::Selectable(tmp);
				ImGui::PopID();
				if(ImGui::IsItemHovered()){
					if(ImGui::IsMouseClicked(1))
						b->Select();
					if(ImGui::IsMouseDoubleClicked(0)){
						rw::V3d pos = add(*(rw::V3d*)&b->inst->matrix.pos, b->sect->origin);
						TheCamera.setTarget(pos);
						TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(halfFloatToFloat(b->inst->bound[3])));
					}
				}
			}
		}
		ImGui::TreePop();
	}

#ifdef EDIT_LINKS
	if(ImGui::Button("Write Links"))
		WriteLinks();

	if(b && ImGui::Button("Unlink"))
		b->SetEntity(-1);

	if(EntityExt::selection.count() == 1 &&
	   BuildingExt::selection.count() == 1){
		static char *failtext = "";	// not used after all....
		if(ImGui::Button("Link")){
			EntityExt *ee = (EntityExt*)e->vtable;
			printf("%p %x\n", b, ee->GetIplID());
			b->SetEntity(ee->GetIplID());
		}else if(!ImGui::IsItemHovered())
			failtext = "";
		ImGui::Text(failtext);
	}
#endif
}

#ifdef VCS
static void
doswap(uint32 group, int state)
{
	int i;
	for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
		SwapInfo *si = &gLevel->chunk->swapInfos[i];
		if(si->hash == group){
			if(state == 0)
				si->building->modelIndex = si->modelA;
			else if(state == si->swapState)
				si->building->modelIndex = si->modelB;
			swapstate[si->swapSlot] = state;
		}
	}
}

uint32 selectedHash = -1;
static void
uiSwap(void)
{
	int i;

	if(ImGui::TreeNode("Swap groups")){
		int numGroups = 0;
		uint32 lastHash = -1;
		lastHash = -1;
		for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
			if(lastHash == gLevel->chunk->swapInfos[i].hash)
				continue;
			lastHash = gLevel->chunk->swapInfos[i].hash;
			numGroups++;
		}
		int nPerCol = (numGroups+2)/3;

		ImGui::Columns(3, "swapgroups", false);
		ImGui::Separator();
		lastHash = -1;
		int n = 0;
		for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
			char label[32];
			if(lastHash == gLevel->chunk->swapInfos[i].hash)
				continue;
			lastHash = gLevel->chunk->swapInfos[i].hash;
			sprintf(label, "%X", gLevel->chunk->swapInfos[i].hash);
			if(ImGui::Selectable(label))
				selectedHash = gLevel->chunk->swapInfos[i].hash;
			//if (ImGui::Button(label, ImVec2(-1,0))) {}
			if(++n == nPerCol){
				ImGui::NextColumn();
				n = 0;
			}
		}
		ImGui::Columns(1);
		ImGui::Separator();
		ImGui::TreePop();
	}

	if(selectedHash != -1){
		static char tmp[128];
		sprintf(tmp, "Group: %X", selectedHash);
		ImGui::Text(tmp);
		ImGui::Columns(5, "thisgroup", true);
		ImGui::Separator();
		ImGui::Text("Entity"); ImGui::NextColumn();
		ImGui::Text("slot"); ImGui::NextColumn();
		ImGui::Text("state"); ImGui::NextColumn();
		ImGui::Text("model A"); ImGui::NextColumn();
		ImGui::Text("model B"); ImGui::NextColumn();
		ImGui::Separator();
		for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
			SwapInfo *si = &gLevel->chunk->swapInfos[i];
			if(si->hash != selectedHash)
				continue;

//			sprintf(tmp, "%p", si->building);
			sprintf(tmp, "0x%X", ((EntityExt*)si->building->vtable)->GetIplID());
			EntityExt *ee = (EntityExt*)si->building->vtable;
			ImGui::Selectable(tmp);
			if(ImGui::IsItemHovered()){
				ee->highlight = 1;
				if(ImGui::IsMouseClicked(1))
					ee->Select();
				if(ImGui::IsMouseClicked(0))
					ee->JumpTo();
			}
			ImGui::NextColumn();
			sprintf(tmp, "%d", si->swapSlot);
			ImGui::Text(tmp);
			ImGui::NextColumn();
			sprintf(tmp, "%d##swap%d", si->swapState, i);
			bool highlight = swapstate[si->swapSlot] == si->swapState;
			if(highlight)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
			if(ImGui::Selectable(tmp)){
				if(swapstate[si->swapSlot] == si->swapState)
					doswap(selectedHash, 0);
				else
					doswap(selectedHash, si->swapState);
/*
				if(swapstate[si->swapSlot] == si->swapState){
					si->building->modelIndex = si->modelA;
					swapstate[si->swapSlot] = 0;
				}else{
					si->building->modelIndex = si->modelB;
					swapstate[si->swapSlot] = si->swapState;
				}
*/
			}
			if(highlight)
				ImGui::PopStyleColor();
			ImGui::NextColumn();

			CBaseModelInfo *miA = CModelInfo::Get(si->modelA);
			CBaseModelInfo *miB = CModelInfo::Get(si->modelB);
			ImGui::Text(miA->name);
			ImGui::NextColumn();
			ImGui::Text(miB->name);
			ImGui::NextColumn();
		}
		ImGui::Columns(1);
		ImGui::Separator();
	}
	if(ImGui::Button("Reset swaps")){
		for(i = 0; i < nelem(swapstate); i++)
			swapstate[i] = 0;
		for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
			SwapInfo *si = &gLevel->chunk->swapInfos[i];
			si->building->modelIndex = si->modelA;
		}
	}
}
#endif

void
gui(void)
{
	if(CPad::IsKeyJustDown('T')) showTimeWeatherWindow ^= 1;
	if(showTimeWeatherWindow){
		ImGui::Begin("Time & Weather", &showTimeWeatherWindow);
		uiTimeWeather();
		ImGui::End();
	}

	if(CPad::IsKeyJustDown('V')) showViewWindow ^= 1;
	if(showViewWindow){
		ImGui::Begin("View", &showViewWindow);
		uiView();
		ImGui::End();
	}

	if(CPad::IsKeyJustDown('I')) showObjectWindow ^= 1;
	if(showObjectWindow){
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin("Object", &showObjectWindow);
		uiObject();
		ImGui::End();
	}

#ifdef VCS
	if(CPad::IsKeyJustDown('X')) showSwapWindow ^= 1;
	if(showSwapWindow){
		ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
		ImGui::Begin("Swap", &showSwapWindow);
		uiSwap();
		ImGui::End();
	}
#endif VCS	

#ifdef EDIT_LINKS
	if(CPad::IsKeyJustDown('H')){
		drawCol = !drawCol;
		drawWorld = !drawCol;
	}
	if(CPad::IsKeyJustDown('B'))
		drawBounds = !drawBounds;

	if(CPad::IsKeyJustDown('C') &&
	   EntityExt::selection.count() == 1 &&
	   BuildingExt::selection.count() == 1){
		BuildingExt *b = BuildingExt::GetSelection();
		CEntity *e = EntityExt::GetSelection();
		EntityExt *ee = (EntityExt*)e->vtable;
		printf("%p %x\n", b, ee->GetIplID());
		b->SetEntity(ee->GetIplID());
	}
#endif
}

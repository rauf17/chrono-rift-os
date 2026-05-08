#pragma once

#include "../shared/shared_state.h"

bool tryAcquireArtifact(GlobalState* state, int entity_index, int artifact_index);
void releaseArtifact(GlobalState* state, int entity_index, int artifact_index);
void introduceEclipseRelic(GlobalState* state);

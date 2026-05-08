#include "artifact_manager.h"

#include <cstring>

static void appendLogUnsafeLocal(GlobalState* state, const char* message) {
    std::strncpy(state->log[state->log_head % ACTION_LOG_LINES], message, ACTION_LOG_WIDTH - 1);
    state->log[state->log_head % ACTION_LOG_LINES][ACTION_LOG_WIDTH - 1] = '\0';
    state->log_head++;
}

bool tryAcquireArtifact(GlobalState* state, int entity_index, int artifact_index) {
    if (!state || entity_index < 0 || entity_index >= MAX_ENTITIES || artifact_index < 0 || artifact_index >= MAX_ARTIFACTS) {
        return false;
    }

    sem_wait(&state->global_mutex);
    ArtifactEntry& entry = state->artifacts[artifact_index];
    if (entry.exists && entry.is_free) {
        entry.is_free = false;
        entry.held_by = entity_index;
        switch (artifact_index) {
            case 0:
                state->entities[entity_index].holds_solar_core = true;
                break;
            case 1:
                state->entities[entity_index].holds_lunar_blade = true;
                break;
            case 2:
                state->entities[entity_index].holds_eclipse_relic = true;
                break;
            default:
                break;
        }
        sem_post(&state->global_mutex);
        return true;
    }

    entry.waiting[entity_index] = true;
    sem_post(&state->global_mutex);
    return false;
}

void releaseArtifact(GlobalState* state, int entity_index, int artifact_index) {
    if (!state || entity_index < 0 || entity_index >= MAX_ENTITIES || artifact_index < 0 || artifact_index >= MAX_ARTIFACTS) {
        return;
    }

    sem_wait(&state->global_mutex);
    ArtifactEntry& entry = state->artifacts[artifact_index];
    entry.is_free = true;
    entry.held_by = -1;
    entry.waiting[entity_index] = false;
    switch (artifact_index) {
        case 0:
            state->entities[entity_index].holds_solar_core = false;
            break;
        case 1:
            state->entities[entity_index].holds_lunar_blade = false;
            break;
        case 2:
            state->entities[entity_index].holds_eclipse_relic = false;
            break;
        default:
            break;
    }
    sem_post(&state->global_mutex);
}

void introduceEclipseRelic(GlobalState* state) {
    if (!state) {
        return;
    }

    sem_wait(&state->global_mutex);
    ArtifactEntry& entry = state->artifacts[2];
    entry.exists = true;
    entry.is_free = true;
    entry.held_by = -1;
    appendLogUnsafeLocal(state, "The Eclipse Relic has appeared.");
    sem_post(&state->global_mutex);
}

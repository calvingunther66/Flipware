#pragma once

#include <gui/scene_manager.h>

void infrared_scene_universal_common_on_enter(void* context);
bool infrared_scene_universal_common_on_event(void* context, SceneManagerEvent event);
void infrared_scene_universal_common_on_exit(void* context);
void infrared_scene_universal_common_item_callback(void* context, uint32_t index, InputType type);

/**
 * @brief Scene state that tells a universal remote scene it is being returned to after a save.
 *
 * Scenes that ask for a file on enter (Universal From File) skip the browser when they see it.
 */
#define INFRARED_SCENE_UNIVERSAL_STATE_RETURNING (0xFFFFFFFFUL)

/**
 * @brief Go back to the universal remote that the save flow was started from.
 *
 * @param[in] context pointer to the application instance.
 */
void infrared_scene_universal_common_return(void* context);

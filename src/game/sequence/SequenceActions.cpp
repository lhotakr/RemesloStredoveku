#include "SequenceActions.h"

#include <algorithm>

namespace sequence
{
    const std::vector<ActionTypeInfo>& AllActionTypes()
    {
        static const std::vector<ActionTypeInfo> kTypes = {
            {"Flow", "wait"},
            {"Flow", "parallel"},
            {"Flow", "goto"},
            {"Flow", "end_sequence"},
            {"Flow", "set_flag"},
            {"Flow", "require_flag"},
            {"Screen", "fade_in"},
            {"Screen", "fade_out"},
            {"Screen", "screen_flash"},
            {"Screen", "show_text"},
            {"Screen", "hide_text"},
            {"Camera", "set_camera"},
            {"Camera", "camera_move"},
            {"Camera", "camera_follow"},
            {"Camera", "camera_shake"},
            {"Actor", "spawn_actor"},
            {"Actor", "remove_actor"},
            {"Actor", "move_actor"},
            {"Actor", "set_actor_animation"},
            {"Actor", "face_actor"},
            {"Audio", "play_music"},
            {"Audio", "stop_music"},
            {"Audio", "play_sfx"},
            {"Audio", "play_voice"},
            {"World", "set_time"},
            {"World", "set_weather"},
            {"World", "load_map"},
            {"Game", "teleport_player"},
            {"Game", "enable_player_control"},
            {"Game", "disable_player_control"},
            {"Game", "start_dialog"},
            {"Game", "change_renderer_mode"},
        };
        return kTypes;
    }

    bool IsKnownActionType(const std::string& type)
    {
        const auto& types = AllActionTypes();
        return std::any_of(types.begin(), types.end(), [&](const ActionTypeInfo& info)
        {
            return type == info.type;
        });
    }
}

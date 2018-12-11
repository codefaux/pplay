//
// Created by cpasjuste on 03/10/18.
//

#ifndef PPLAY_PLAYER_H
#define PPLAY_PLAYER_H

extern "C" {
#include "kitchensink/kitchensink.h"
#include "kitchensink/internal/kitlibstate.h"
}

#include "subtitles_texture.h"
#include "menus/menu_video_submenu.h"
#include "video_texture.h"

#define AUDIO_BUFFER_SIZE (1024 * 64)
#define MAX_STREAM_LIST_SIZE 32

class Main;

class PlayerOSD;

class Player : public c2d::Rectangle {

public:

    class Stream {
    public:
        void reset() {
            memset(streams, 0, MAX_STREAM_LIST_SIZE * sizeof(int));
            size = 0;
            current = 0;
        }

        int getCurrentStream() {
            if (current >= 0 && current < size) {
                return streams[current];
            }
            return -1;
        }

        int streams[MAX_STREAM_LIST_SIZE];
        int size = 0;
        int current = 0;
    };

    enum class CpuClock {
        Min = 0,
        Max = 1
    };

    explicit Player(Main *main);

    ~Player();

    bool load(const MediaFile &file);

    void pause();

    void resume();

    void stop();

    bool isPlaying();

    bool isPaused();

    bool isFullscreen();

    void setFullscreen(bool maximize);

    void setVideoStream(int index);

    void setAudioStream(int index);

    void setSubtitleStream(int index);

    void setCpuClock(const CpuClock &clock);

    Main *getMain();

    VideoTexture *getVideoTexture();

    c2d::TweenPosition *getTweenPosition();

    c2d::TweenScale *getTweenScale();

    MenuVideoSubmenu *getMenuVideoStreams();

    MenuVideoSubmenu *getMenuAudioStreams();

    MenuVideoSubmenu *getMenuSubtitlesStreams();

    Stream *getVideoStreams();

    Stream *getAudioStreams();

    Stream *getSubtitlesStreams();

    bool onInput(c2d::Input::Player *players) override;

//private:

    void onDraw(c2d::Transform &transform) override;

    Main *main = nullptr;
    PlayerOSD *osd = nullptr;
    VideoTexture *texture = nullptr;
    SubtitlesTexture *textureSub = nullptr;
    c2d::TweenPosition *tweenPosition = nullptr;
    c2d::TweenScale *tweenScale = nullptr;
    MenuVideoSubmenu *menuVideoStreams = nullptr;
    MenuVideoSubmenu *menuAudioStreams = nullptr;
    MenuVideoSubmenu *menuSubtitlesStreams = nullptr;

    // kit player
    Kit_Source *source = nullptr;
    Kit_Player *player = nullptr;
    Kit_PlayerInfo playerInfo;
    Stream video_streams;
    Stream audio_streams;
    Stream subtitles_streams;

    // audio
    SDL_AudioDeviceID audioDeviceID = 0;
    char audioBuffer[AUDIO_BUFFER_SIZE];

    bool show_subtitles = false;
    bool fullscreen = false;
};

#endif //PPLAY_PLAYER_H

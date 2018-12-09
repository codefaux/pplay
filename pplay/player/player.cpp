//
// Created by cpasjuste on 03/10/18.
//

#include <sstream>
#include <iomanip>

#include "main.h"
#include "player.h"

using namespace c2d;

Player::Player(Main *_main) : Rectangle(_main->getSize()) {

    main = _main;

    setOrigin(Origin::TopRight);

    tweenPosition = new TweenPosition(
            {main->getSize().x - 32, 32},
            {main->getSize().x, 0}, 0.5f);
    add(tweenPosition);
    tweenScale = new TweenScale({0.6f, 0.6f}, {1.0f, 1.0f}, 0.5f);
    add(tweenScale);

    osd = new PlayerOSD(this);
    add(osd);

    setVisibility(Visibility::Hidden);
}

Player::~Player() {
    stop();
}

bool Player::load(const MediaFile &file) {

    stop();

    // avformat_network_init/deinit handled in media info thread
    int err = Kit_Init(KIT_INIT_ASS);
    if (err != 0) {
        printf("unable to initialize Kitchensink: %s\n", Kit_GetError());
        stop();
        return false;
    }

    // open source file
    printf("Player::load: %s\n", file.path.c_str());
    source = Kit_CreateSourceFromUrl(file.path.c_str());
    if (!source) {
        printf("unable to load '%s': %s\n", file.path.c_str(), Kit_GetError());
        stop();
        return false;
    }

    // find available streams
    video_streams.size = Kit_GetSourceStreamList(
            source, KIT_STREAMTYPE_VIDEO, video_streams.streams, MAX_STREAM_LIST_SIZE);
    audio_streams.size = Kit_GetSourceStreamList(
            source, KIT_STREAMTYPE_AUDIO, audio_streams.streams, MAX_STREAM_LIST_SIZE);
    subtitles_streams.size = Kit_GetSourceStreamList(
            source, KIT_STREAMTYPE_SUBTITLE, subtitles_streams.streams, MAX_STREAM_LIST_SIZE);
    printf("Player::load: \n\tVIDEO STREAMS: %i\n\tAUDIO STREAMS: %i\n\tSUBTITLES STREAMS: %i\n",
           video_streams.size, audio_streams.size, subtitles_streams.size);
    if (!video_streams.size && !audio_streams.size) {
        printf("no usable audio or video stream found: %s\n", Kit_GetError());
        stop();
        return false;
    }

    // set default font
    Kit_LibraryState *state = Kit_GetLibraryState();
    snprintf(state->subtitle_font_path, 511, "%sskin/font.ttf",
             getMain()->getIo()->getDataReadPath().c_str());

    // create the player
    player = Kit_CreatePlayer(
            source,
            video_streams.getCurrentStream(),
            audio_streams.getCurrentStream(),
            subtitles_streams.getCurrentStream(),
            (int) getSize().x, (int) getSize().y);
    if (!player) {
        printf("unable to create player: %s\n", Kit_GetError());
        stop();
        return false;
    }

    // get some information
    Kit_GetPlayerInfo(player, &playerInfo);
    printf("Video(%s, %s): %i x %i , Audio(%s): %i hz\n",
           playerInfo.video.codec.name,
           SDL_GetPixelFormatName(playerInfo.video.output.format),
           playerInfo.video.output.width, playerInfo.video.output.height,
           playerInfo.audio.codec.name,
           playerInfo.audio.output.samplerate);

    if (audio_streams.size > 0) {
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_AudioSpec wanted_spec, audio_spec;
        SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
        wanted_spec.freq = playerInfo.audio.output.samplerate;
        wanted_spec.format = (SDL_AudioFormat) playerInfo.audio.output.format;
        wanted_spec.channels = (Uint8) playerInfo.audio.output.channels;
        audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &audio_spec, 0);
        SDL_PauseAudioDevice(audioDeviceID, 0);

        // audios menu options
        std::vector<MenuItem> items;
        for (auto &stream : file.media.audios) {
            items.emplace_back("Lang: " + stream.language, "", MenuItem::Position::Top, stream.id);
        }
        menuAudioStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), items, MENU_VIDEO_TYPE_AUD);
        menuAudioStreams->setVisibility(Visibility::Hidden, false);
        menuAudioStreams->setLayer(3);
        add(menuAudioStreams);
    }

    if (video_streams.size > 0) {
        texture = new C2DTexture(
                {playerInfo.video.output.width, playerInfo.video.output.height}, Texture::Format::RGBA8);
        texture->setDeleteMode(DeleteMode::Manual);
        texture->setFilter(Texture::Filter::Linear);
        //setSize(texture->getSize());
        //setOrigin(Origin::TopRight);
        add(texture);

        // videos menu options
        std::vector<MenuItem> items;
        for (auto &stream : file.media.videos) {
            items.emplace_back("Lang: " + stream.language, "", MenuItem::Position::Top, stream.id);
        }
        menuVideoStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), items, MENU_VIDEO_TYPE_VID);
        menuVideoStreams->setVisibility(Visibility::Hidden, false);
        menuVideoStreams->setLayer(3);
        add(menuVideoStreams);
    }

    if (subtitles_streams.size > 0) {
        textureSub = new SubtitlesTexture();
        textureSub->setDeleteMode(DeleteMode::Manual);
        void *buf;
        textureSub->lock(nullptr, &buf, nullptr);
        memset(buf, 0, 1024 * 1024 * 4);
        textureSub->unlock();
        textureSub->setFilter(Texture::Filter::Point);
        textureSub->setVisibility(Visibility::Hidden);
        add(textureSub);

        // subtitles menu options
        std::vector<MenuItem> items;
        items.emplace_back("None", "", MenuItem::Position::Top, -1);
        for (auto &stream : file.media.subtitles) {
            items.emplace_back("Lang: " + stream.language, "", MenuItem::Position::Top, stream.id);
        }
        menuSubtitlesStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), items, MENU_VIDEO_TYPE_SUB);
        menuSubtitlesStreams->setVisibility(Visibility::Hidden, false);
        menuSubtitlesStreams->setLayer(3);
        add(menuSubtitlesStreams);
    }

    setVisibility(Visibility::Visible);
    osd->setLayer(3);

    // start playback
    Kit_PlayerPlay(player);

    setCpuClock(CpuClock::Max);

    return true;
}

bool Player::onInput(c2d::Input::Player *players) {

    if (main->getFiler()->isVisible() || main->getMenuVideo()->isVisible()
        || (getMenuVideoStreams() && getMenuVideoStreams()->isVisible())
        || (getMenuAudioStreams() && getMenuAudioStreams()->isVisible())
        || (getMenuSubtitlesStreams() && getMenuSubtitlesStreams()->isVisible())) {
        return C2DObject::onInput(players);
    }

    //////////////////
    /// handle inputs
    //////////////////
    unsigned int keys = players[0].keys;

    if (keys & Input::Key::Fire1) {
        if (osd->isVisible()) {
            if (isPaused()) {
                resume();
                osd->resume();
            } else {
                pause();
                osd->pause();
            }
        } else {
            osd->setVisibility(Visibility::Visible, true);
        }
    } else if (keys & Input::Key::Fire2) {
        if (osd->isVisible()) {
            osd->setVisibility(Visibility::Hidden, true);
        } else {
            osd->setVisibility(Visibility::Visible, true);
        }
    } else {

        double position = Kit_GetPlayerPosition(player);
        double duration = Kit_GetPlayerDuration(player);

        if (keys & c2d::Input::Key::Left) {
            // TODO: seek
            //osd->setVisibility(Visibility::Visible, true);
            //Kit_PlayerSeek(player, position - 60.0);
            main->setPlayerFullscreen(false);

        } else if (keys & c2d::Input::Key::Right) {
            main->getMenuVideo()->setVisibility(Visibility::Visible, true);
            //osd->setVisibility(Visibility::Visible, true);
            //if (position + 60 < duration) {
            //    Kit_PlayerSeek(player, position + 60.0);
            //}
        } else if (keys & c2d::Input::Key::Up) {
            osd->setVisibility(Visibility::Visible, true);
            if (position + (60.0 * 10.0) < duration) {
                Kit_PlayerSeek(player, position + (60.0 * 10.0));
            }
        } else if (keys & c2d::Input::Key::Down) {
            osd->setVisibility(Visibility::Visible, true);
            Kit_PlayerSeek(player, position - (60.0 * 10.0));
        }
    }

    return true;
}

void Player::onDraw(c2d::Transform &transform) {

    if (!isPlaying()) {
        if (isFullscreen()) {
            main->setPlayerFullscreen(false);
        }
        Rectangle::onDraw(transform);
        return;
    }

    /// OSD
    double position = Kit_GetPlayerPosition(player);
    double duration = Kit_GetPlayerDuration(player);
    osd->setProgress((float) duration, (float) position);

    //////////////////
    /// step ffmpeg
    //////////////////
    /// audio
    if (audio_streams.size > 0) {
        int queued = SDL_GetQueuedAudioSize(audioDeviceID);
        if (queued < AUDIO_BUFFER_SIZE) {
            int need = AUDIO_BUFFER_SIZE - queued;
            while (need > 0) {
                int ret = Kit_GetPlayerAudioData(
                        player, (unsigned char *) audioBuffer, AUDIO_BUFFER_SIZE);
                need -= ret;
                if (ret > 0) {
                    SDL_QueueAudio(audioDeviceID, audioBuffer, (Uint32) ret);
                } else {
                    break;
                }
            }
        }
    }

    /// video
    if (video_streams.size > 0) {
        void *video_data;
        texture->lock(nullptr, &video_data, nullptr);
        if (Kit_GetPlayerVideoDataRaw(player, video_data)) {
            texture->unlock();
        }
        // scaling
        Vector2f max_scale = {
                getSize().x / texture->getTextureRect().width,
                getSize().y / texture->getTextureRect().height};
        Vector2f scale = {max_scale.y, max_scale.y};
        if (scale.x > max_scale.x) {
            scale.x = scale.y = max_scale.x;
        }
        texture->setOrigin(Origin::Center);
        texture->setPosition(getSize().x / 2.0f, getSize().y / 2.0f);
        texture->setScale(scale);
    }

    /// Subtitles
    if (show_subtitles && subtitles_streams.size > 0) {
        int count = Kit_GetPlayerSubtitleDataRaw(
                player, textureSub->pixels, textureSub->getRectsSrc(), textureSub->getRectsDst(), ATLAS_MAX);
        textureSub->setRectsCount(count);
        if (count > 0) {
            textureSub->unlock();
        }
    }

    Rectangle::onDraw(transform);
}

void Player::setVideoStream(int index) {
    if (texture && index > -1) {
        texture->setVisibility(Visibility::Visible);
        Kit_SetPlayerStream(player, KIT_STREAMTYPE_VIDEO, index);
    } else {
        if (texture) {
            texture->setVisibility(Visibility::Hidden);
        }
    }
}

void Player::setAudioStream(int index) {
    if (index > -1) {
        Kit_SetPlayerStream(player, KIT_STREAMTYPE_AUDIO, index);
    }
}

void Player::setSubtitleStream(int index) {
    if (textureSub && index > -1) {
        show_subtitles = true;
        textureSub->setVisibility(Visibility::Visible);
        Kit_SetPlayerStream(player, KIT_STREAMTYPE_SUBTITLE, index);
    } else {
        show_subtitles = false;
        if (textureSub) {
            textureSub->setVisibility(Visibility::Hidden);
        }
    }
}

bool Player::isPlaying() {

    return player != nullptr
           && (Kit_GetPlayerState(player) == KIT_PLAYING
               || Kit_GetPlayerState(player) == KIT_PAUSED);
}

bool Player::isPaused() {

    return player != nullptr
           && Kit_GetPlayerState(player) == KIT_PAUSED;
}

bool Player::isFullscreen() {
    return fullscreen;
}

void Player::setFullscreen(bool fs) {

    fullscreen = fs;

    if (!fullscreen) {
        main->getMenuVideo()->setVisibility(Visibility::Hidden, true);
        if (getMenuVideoStreams()) {
            getMenuVideoStreams()->setVisibility(Visibility::Hidden, true);
        }
        if (getMenuAudioStreams()) {
            getMenuAudioStreams()->setVisibility(Visibility::Hidden, true);
        }
        if (getMenuSubtitlesStreams()) {
            getMenuSubtitlesStreams()->setVisibility(Visibility::Hidden, true);
        }
    }
}

void Player::pause() {

    if (!isPaused()) {
        Kit_PlayerPause(player);
    }

    setCpuClock(CpuClock::Min);
}

void Player::resume() {

    if (isPaused()) {
        Kit_PlayerPlay(player);
    }

    setCpuClock(CpuClock::Max);
}

void Player::stop() {

    /// Kit
    if (player) {
        Kit_PlayerStop(player);
        Kit_ClosePlayer(player);
        player = nullptr;
    }
    if (source) {
        Kit_CloseSource(source);
        source = nullptr;
    }
    Kit_Quit();

    /// Audio
    if (audioDeviceID > 0) {
        SDL_CloseAudioDevice(audioDeviceID);
        audioDeviceID = 0;
    }
    if (menuAudioStreams) {
        delete (menuAudioStreams);
        menuAudioStreams = nullptr;
    }


    /// Video
    if (texture) {
        delete (texture);
        texture = nullptr;
    }
    if (menuVideoStreams) {
        delete (menuVideoStreams);
        menuVideoStreams = nullptr;
    }

    /// Subtitles
    if (textureSub) {
        delete (textureSub);
        textureSub = nullptr;
    }
    if (menuSubtitlesStreams) {
        delete (menuSubtitlesStreams);
        menuSubtitlesStreams = nullptr;
    }

    video_streams.reset();
    audio_streams.reset();
    subtitles_streams.reset();
    show_subtitles = false;

    setCpuClock(CpuClock::Min);
}

void Player::setCpuClock(const CpuClock &clock) {
#ifdef __SWITCH__
    if (clock == CpuClock::Min) {
        if (SwitchSys::getClock(SwitchSys::Module::Cpu) != SwitchSys::getClockStock(SwitchSys::Module::Cpu)) {
            int clock_old = SwitchSys::getClock(SwitchSys::Module::Cpu);
            SwitchSys::setClock(SwitchSys::Module::Cpu, (int) SwitchSys::CPUClock::Stock);
            printf("restored cpu speed (old: %i, new: %i)\n",
                   clock_old, SwitchSys::getClock(SwitchSys::Module::Cpu));
        }
    } else {
        if (playerInfo.video.output.width > 1280
            || playerInfo.video.output.height > 720) {
            int clock_old = SwitchSys::getClock(SwitchSys::Module::Cpu);
            SwitchSys::setClock(SwitchSys::Module::Cpu, (int) SwitchSys::CPUClock::Max);
            printf("fhd video spotted, setting max cpu speed (old: %i, new: %i)\n",
                   clock_old, SwitchSys::getClock(SwitchSys::Module::Cpu));
        }
    }
#endif
}

Main *Player::getMain() {
    return main;
}

c2d::TweenPosition *Player::getTweenPosition() {
    return tweenPosition;
}

c2d::TweenScale *Player::getTweenScale() {
    return tweenScale;
}

MenuVideoSubmenu *Player::getMenuVideoStreams() {
    return menuVideoStreams;
}

MenuVideoSubmenu *Player::getMenuAudioStreams() {
    return menuAudioStreams;
}

MenuVideoSubmenu *Player::getMenuSubtitlesStreams() {
    return menuSubtitlesStreams;
}

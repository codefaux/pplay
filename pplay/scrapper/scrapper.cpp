//
// Created by cpasjuste on 29/03/19.
//

#include <utility.h>
#include "main.h"
#include "scrapper.h"
#include "p_search.h"

using namespace pplay;
using namespace pscrap;

#define API_KEY "80435e61cc52ad8850355379108a92d0"

static std::vector<c2d::Io::File> scrapList;

static void getMedias(Main *main, const std::string &path) {

    std::vector<std::string> ext = pplay::Utility::getMediaExtensions();
    pplay::Io::DeviceType type = ((pplay::Io *) main->getIo())->getType(path);
    std::vector<c2d::Io::File> files =
            ((pplay::Io *) main->getIo())->getDirList(type, ext, path);

    if (files.empty()) {
        return;
    }

    for (auto &file : files) {
        if (!main->getScrapper()->running) {
            break;
        }
        if (file.type == c2d::Io::Type::Directory) {
            if (file.name == "." || file.name == "..") {
                continue;
            }
            getMedias(main, file.path);
        } else {
            scrapList.emplace_back(file);
        }
    }
}

static const char *tokens[64] = {
        "720p", "1080p", "2160p"
};

static int scrap_thread(void *ptr) {

    auto scrapper = (Scrapper *) ptr;
    auto main = (Main *) scrapper->main;

    while (scrapper->running) {

        SDL_LockMutex(scrapper->mutex);
        SDL_CondWait(scrapper->cond, scrapper->mutex);

#ifdef __SWITCH__
        appletSetMediaPlaybackState(true);
#endif
        if (scrapper->running) {

            scrapper->scrapping = true;
            main->getStatus()->show("Scrapping...", "Building media list...", true);

            c2d::C2DClock clock;

            scrapList.clear();
            getMedias(main, scrapper->path);

            size_t size = scrapList.size();
            for (size_t i = 0; i < size; i++) {
                if (!main->getScrapper()->running) {
                    break;
                }
                c2d::Io::File file = scrapList.at(i);
                std::string scrap_path = pplay::Utility::getMediaScrapPath(file);
                if (!main->getIo()->exist(scrap_path)) {
                    std::string title = "Scrapping... (" + std::to_string(i) + "/" + std::to_string(size) + ")";
                    main->getStatus()->show(title, "Searching: " + file.name, true);
                    std::string lang = main->getConfig()->getOption(OPT_TMDB_LANGUAGE)->getString();
                    // try to cut at unneeded tokens to speed up "recursive name" search
                    // will work until 2030 :)
                    bool cutted = false;
                    std::string toSearch = c2d::Utility::toLower(file.name);
                    toSearch = c2d::Utility::removeExt(toSearch);
                    for (int j = 2030; j > 1969; j--) {
                        size_t pos = toSearch.rfind(std::to_string(j));
                        if (pos != std::string::npos & pos > 3) {
                            toSearch = toSearch.substr(0, pos - 1);
                            cutted = true;
                            break;
                        }
                    }
                    if (!cutted) {
                        for (int j = 0; j < 32; j++) {
                            if (tokens[j]) {
                                size_t pos = toSearch.rfind(tokens[j]);
                                if (pos != std::string::npos && pos > 1) {
                                    toSearch = toSearch.substr(0, pos - 1);
                                    break;
                                }
                            }
                        }
                    }
                    Search search(API_KEY, toSearch, lang);
                    int res = search.get();
                    if (res == 0) {
                        search.save(scrap_path);
                        if (search.total_results > 0) {
                            printf("%s\n", search.movies.at(0).title.c_str());
                            main->getStatus()->show(title, "Downloading poster: "
                                                           + search.movies.at(0).title, true);
                            search.movies.at(0).getPoster(pplay::Utility::getMediaPosterPath(file));
                            main->getStatus()->show(title, "Downloading backdrop: "
                                                           + search.movies.at(0).title, true);
                            search.movies.at(0).getBackdrop(pplay::Utility::getMediaBackdropPath(file), 780);
                        }
                    }
                }
            }

            scrapper->main->getStatus()->show(
                    "Scrapping...", "Done in "
                                    + pplay::Utility::formatTime(clock.getElapsedTime().asSeconds()));
            scrapper->scrapping = false;
        }

#ifdef __SWITCH__
        appletSetMediaPlaybackState(false);
#endif
        SDL_UnlockMutex(scrapper->mutex);
    }

    return 0;
}

Scrapper::Scrapper(Main *m) {

    main = m;
    mutex = SDL_CreateMutex();
    cond = SDL_CreateCond();
    thread = SDL_CreateThread(scrap_thread, "scrap_thread", (void *) this);
}

int Scrapper::scrap(const std::string &p) {

    if (scrapping) {
        return -1;
    }

    path = p;
    SDL_CondSignal(cond);

    return 0;
}

Scrapper::~Scrapper() {

    scrapping = false;
    running = false;
    SDL_CondSignal(cond);
    SDL_WaitThread(thread, nullptr);
    SDL_DestroyCond(cond);
    printf("Scrapper::~Scrapper\n");
}

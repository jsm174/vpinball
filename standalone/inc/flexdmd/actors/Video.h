#pragma once

#include "AnimatedActor.h"
extern "C" {
#include <libavformat/avformat.h>
#include "libavcodec/avcodec.h"
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class Video : public AnimatedActor {
public:
    static Video* Create(FlexDMD* pFlexDMD, AssetManager* pAssetManager, const std::string& path, const std::string& name, bool loop = true);
    ~Video();

    void OnStageStateChanged() override;
    void Rewind() override;
    void ReadNextFrame() override;
    void Draw(VP::SurfaceGraphics* pGraphics) override;

private:
    Video(FlexDMD* pFlexDMD, const std::string& name);

    std::vector<SDL_Surface*> m_frames;
    SDL_Surface* m_pActiveFrameSurface = nullptr;
    int m_pos = 0;
    float m_frameDuration = 0.0f;
    SwsContext* m_pVideoConversionContext = nullptr;
    AVFormatContext* m_pFormatContext = nullptr;
    AVCodecContext* m_pCodecContext = nullptr;
    int m_videoStreamIndex = -1;
};

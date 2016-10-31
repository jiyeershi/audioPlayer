#ifndef __AudioPlayer__
#define __AudioPlayer__

#include <string>
#include <queue>
#include <mutex>
#include "RubberBandStretcher.h"
#include "sndfile.h"
#include "cocos2d.h"

struct audioDataFrame {
    float* data;
    int size;
};

class WanakaAudioPlayer;
class AudioPlayer {
public:
    AudioPlayer();
    virtual ~AudioPlayer();

    //static AudioPlayer* getInstance();

    //播放控制
    void play(const char* audioPath, float timeScale = 1.f, bool isLoop = false); //开始播放或取消暂停 isLoop为true则循环播放
    void pause();
    void resume();
    void stop();
    void seek(float second);
    bool isPlaying() { return _isPlaying; }

    float getDuration();  //获取总时长（单位：秒）
    float getCurrent();   //获取当前播放了的时长（单位：秒）
    void setVolume(float volume); //v的范围为0-1.f

    void setDelegate(WanakaAudioPlayer* delegate){ _delegate = delegate; };
private:
    bool initAudio(const char* audioPath);
    bool initAudioContext();
    void strechProcess();
    void seekProcess();
    void timeCalculate(); //播放时间累积计算
    void process();

    //清空变速数据缓存
    void reset();
    void clearCache();

    //音频线程
    friend/* static */void processAudio(AudioPlayer* player, uint8_t* stream, int len);

private:
    bool _isPlaying;
    bool _quit;
    bool _isLoop;
    bool _audioOk;
    bool _isStrechProcessing;
    bool _isSeek;
    double _seekSecond;
    double _elapsedTime;
    double _totalTime;
    double _totalDataSize;
    double _curPlayIndex;
    double _curPlayTick; //记录sdl取数据的时刻，默认为播放时刻
    int _skipDataSize; //在数据拉伸处理正在进行时，进行seek操作，跳过的数据量, 单位：sizeof(float)
    double _pauseTick;
    double _totalPauseTime; //sdl在播放每次得到的数据的过程中暂停的总时间
    std::string _fullPath;
    float _timeScale;
    float _volume;
    double _sdlUseDataInterval; //sdl使用数据时长,在sdl线程中赋值
    RubberBand::RubberBandStretcher* _rubberStrecher;
    std::vector<audioDataFrame> _audioDataVec;
    //std::mutex _lock;
    std::recursive_mutex _rLock;
    WanakaAudioPlayer* _delegate;
};

#endif /* defined(__AudioPlayer__) */
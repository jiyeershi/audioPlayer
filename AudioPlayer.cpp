#include "AudioPlayer.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include "UtilsWanakaFramework.h"
#include "WanakaAudioPlayer.h"

extern "C" {
    //SDL
#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_thread.h"
}

USING_NS_CC;
using namespace RubberBand;

#define SDL_AUDIO_BUFFER_SIZE 8192
#define MAX_AUDIOQ_SIZE (5 * 16 * 8192)

static int sProcessIndex = 0;
static int sCurCopyIndex = 0;
static double sFirstSoundTime = 0.f;
static double sStartTimePoint = 0.f;
static double sCurTimePoint = 0.f;
static int sSdlBuffLen = 0;
static bool isCurVolumeChanged = false;
//======================================================
//sdl调用的音频处理函数
//======================================================
/*static */void processAudio(AudioPlayer* player, uint8_t* stream, int len) {
    player->process();

    int needCopy = 0;
    bool isCopyComplete = false;
    bool isNeedUpdate = false;
    while (len > 0) {
        std::lock_guard<std::recursive_mutex> lock(player->_rLock);
        if (player->_quit) {
            return;
        }

        if (player->_audioDataVec.size() > sProcessIndex) {
            if (sFirstSoundTime > -0.0000001 && sFirstSoundTime < 0.000001) {
                sFirstSoundTime = UtilsWanakaFramework::getUnixTimestamp();
                sStartTimePoint = sCurTimePoint = sFirstSoundTime;
            }
            audioDataFrame audioData = player->_audioDataVec[sProcessIndex];
            if (!isCurVolumeChanged && player->_volume < 1.f) {//需要调节声音
                //log("player->_audioDataVec.size() = %d, sProcessIndex = %d", player->_audioDataVec.size(), sProcessIndex);
                for (int i = 0; i < audioData.size; i++) {
                    audioData.data[i] *= player->_volume;
                }
                isCurVolumeChanged = true;
            }
            int leftByteOfAudioData = audioData.size * sizeof(float) - sCurCopyIndex;
            if (len >= leftByteOfAudioData) {
                needCopy = leftByteOfAudioData;
                isCopyComplete = true;
            }
            else {
                needCopy = len;
            }
            len -= needCopy;
            memcpy(stream, ((uint8_t*)audioData.data + sCurCopyIndex), needCopy);
            if (!isNeedUpdate) {
                isNeedUpdate = true;
                player->_curPlayTick = UtilsWanakaFramework::getUnixTimestamp();
                //计算当前播放的位置
                int curPlayIndex = player->_skipDataSize;
                for (int i = 0; i < sProcessIndex; i++) {
                    curPlayIndex += player->_audioDataVec[i].size;
                }
                player->_curPlayIndex = curPlayIndex * sizeof(float) + sCurCopyIndex;
                player->_seekSecond = 0.f;
                player->_totalPauseTime = 0.f;
                player->_elapsedTime = 0.f;
                sSdlBuffLen = 0;
            }
            sSdlBuffLen += needCopy;
            stream += needCopy;
            sCurCopyIndex += needCopy;

            if (isCopyComplete) {
                sCurCopyIndex = 0;
                sProcessIndex += 1;
                isCopyComplete = false;
                isCurVolumeChanged = false;
                //log("------------>sProcessIndex = %d", sProcessIndex);
                //当前包处理完了，退出，防止死等
                if (player->_audioDataVec.size() == sProcessIndex) {
                    return;
                }
            }
        }
        else {
            SDL_Delay(100);
        }
    }
    //needCopy = 0;
}

void sdl_audio_callback(void *userdata, Uint8* stream, int len) {
    AudioPlayer* player = (AudioPlayer*)userdata;
    processAudio(player, stream, len);
}
//======================================================

AudioPlayer::AudioPlayer() :
_totalTime(0.f),
_elapsedTime(0.f),
_isPlaying(false),
_isStrechProcessing(false),
_isSeek(false),
_quit(false),
_isLoop(false),
_audioOk(false),
_timeScale(1.f),
_volume(1.f),
_seekSecond(0.f),
_totalDataSize(0),
_skipDataSize(0),
_curPlayIndex(0),
_curPlayTick(0.f),
_pauseTick(0.f),
_totalPauseTime(0.f),
_sdlUseDataInterval(0.f),
_rubberStrecher(nullptr),
_delegate(nullptr) {

}

AudioPlayer::~AudioPlayer() {
    reset();
}

bool AudioPlayer::initAudioContext() {
    SNDFILE *sndfile;
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(sfinfo));
    sndfile = sf_open(_fullPath.c_str(), SFM_READ, &sfinfo);
    _totalTime = double(sfinfo.frames) / double(sfinfo.samplerate);//单位ms
    _totalDataSize = double(sfinfo.frames) * sfinfo.channels; //缩放前float总数

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        log("Could not initialize SDL - %s", SDL_GetError());
        return false;
    }

    SDL_AudioSpec wanted_spec, spec;
    wanted_spec.freq = sfinfo.samplerate;
    wanted_spec.format = AUDIO_F32SYS;
    wanted_spec.channels = sfinfo.channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 4096;//SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    SDL_AudioDeviceID audioID = SDL_OpenAudio(&wanted_spec, &spec);
    if (audioID != 0) {
        const char * errMsg = SDL_GetError();
        log("AudioPlayer: can't open audio, err: %s", errMsg);
        sf_close(sndfile);
        return false;
    }

    if (spec.channels != wanted_spec.channels) {
        sf_close(sndfile);
        log("not support channel");
        return false;
    }

//     if (spec.format != AUDIO_S32SYS && spec.format != AUDIO_S16SYS) {
//         log("wrong format");
//         return false;
//     }

    sf_close(sndfile);
    return true;
}

bool AudioPlayer::initAudio(const char* audioPath) {
    _fullPath =  FileUtils::getInstance()->fullPathForFilename(audioPath);
    if (!(_fullPath.compare(""))) {
        log("audioPath scale input error!!!");
        _delegate->excuteCallback(WanakaAudioPlayer::EventType::LoadError);
        return false;
    }

    if (initAudioContext()) {
        _audioOk = true;
    }
    else {
        _delegate->excuteCallback(WanakaAudioPlayer::EventType::LoadError);
        return false;
    }
    _delegate->excuteCallback(WanakaAudioPlayer::EventType::LoadSuccess);
    return true;
}

void AudioPlayer::play(const char* audioPath, float timeScale/* = 1.f*/, bool isLoop/* = false*/) {
    if (_isPlaying) {
        stop();
        //std::this_thread::sleep_for(std::chrono::milliseconds(10));
       // _quit = false; //重入时将退出标记置false
    }

    std::lock_guard<std::recursive_mutex> lock(_rLock);
    _quit = false; //重入时将退出标记置false
    sProcessIndex = 0;
    sCurCopyIndex = 0;
    sStartTimePoint = sCurTimePoint = sFirstSoundTime = 0.f;
    isCurVolumeChanged = false;

    _isLoop = isLoop;
    _timeScale = timeScale; //0.5 - 2.0
    if (timeScale < 0.5f || timeScale > 2.0f) {
        log("timeScale input error!!!");
        return;
    }
    if (!initAudio(audioPath)) {
        log("initAudio not right!!!");
        return;
    }

    _isPlaying = true;
    _totalDataSize *= timeScale;

    //数据拉伸线程
    std::thread strechThread = std::thread(&AudioPlayer::strechProcess, this);
    strechThread.detach();

    //开启音频流处理线程
    if (_audioOk) {
        SDL_PauseAudio(0);
    }
}

void AudioPlayer::strechProcess() {
    int ibs = 1024;
    bool isScale = (_timeScale != 1.0);
    double pitchshift = 0.0;
    SNDFILE *sndfile;
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(SF_INFO));
    sndfile = sf_open(_fullPath.c_str(), SFM_READ, &sfinfo);
    if (!sndfile) {
        log("ERROR: Failed to open input file!!!");
        return;
    }

    size_t channels = sfinfo.channels;
    RubberBand::RubberBandStretcher::Options options = 0;
    options |= RubberBandStretcher::OptionProcessRealTime;
   // options |= RubberBandStretcher::OptionProcessOffline;
    //options |= RubberBandStretcher::OptionStretchPrecise;
//    options |= RubberBandStretcher::OptionStretchElastic;

    //下面两个选项一起可以去掉变速后的杂声
    options |= RubberBandStretcher::OptionTransientsSmooth;
    options |= RubberBandStretcher::OptionChannelsTogether;

//    options |= RubberBandStretcher::OptionFormantPreserved;

//    options |= RubberBandStretcher::OptionPitchHighQuality;
    //options |= RubberBandStretcher::OptionSmoothingOn;
    //options |= RubberBandStretcher::OptionWindowLong;
    //options |= RubberBandStretcher::OptionPhaseIndependent;
    _rubberStrecher = new RubberBand::RubberBandStretcher(sfinfo.samplerate, channels, options,
        _timeScale);
    //_rubberStrecher->setExpectedInputDuration(sfinfo.frames);

    _isStrechProcessing = true;
    audioDataFrame audioData;
    int frame = 0;
    //int total = 0;
    float *fbuf = new float[channels * ibs];
    float **ibuf = new float *[channels];
    for (size_t i = 0; i < channels; ++i) ibuf[i] = new float[ibs];
    while (frame < sfinfo.frames) {
        std::lock_guard<std::recursive_mutex> lock(_rLock);
        if (_quit) {
            break;
        }

        //处理过程中seek,直接移动到seek的位置进行处理，seek前面的
        if (_isSeek) {
            _rubberStrecher->reset();
            clearCache();
            sProcessIndex = 0;
            sCurCopyIndex = 0;
            int destFrame = _seekSecond / _totalTime * sfinfo.frames;
            if (sf_seek(sndfile, destFrame, SEEK_SET) < 0) {
                break;
            }
            frame = destFrame;
            _skipDataSize = frame * channels * _timeScale;
            _isSeek = false;
        }

        int count = -1;
        if ((count = sf_readf_float(sndfile, fbuf, ibs)) < 0) {
            break;
        };
        if (!isScale) {
            float *fobf = new float[channels * count];
            memcpy(fobf, fbuf, channels * count * sizeof(float));
            //total += count;
            //log("total = %d, sfinfo.frames = %d", total, sfinfo.frames);

            //入队，待数据拷贝到audio
            audioData.data = fobf;
            audioData.size = channels * count;
            //循环播放时_audioDataQueue队列可以一直上涨，直接所有转换完的数据入队，不用睡眠
            _audioDataVec.push_back(audioData);
            if (!_isLoop && _audioDataVec.size() >= MAX_AUDIOQ_SIZE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        else {
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < count; ++i) {
                    float value = fbuf[i * channels + c];
                    ibuf[c][i] = value;
                }
            }
            bool final = (frame + ibs >= sfinfo.frames);
            _rubberStrecher->process(ibuf, count, final);
            _delegate->excuteCallback(WanakaAudioPlayer::EventType::Process);
            int avail = _rubberStrecher->available();
            if (avail > 0) {
                float **obf = new float *[channels];
                for (size_t i = 0; i < channels; ++i) {
                    obf[i] = new float[avail];
                }
                _rubberStrecher->retrieve(obf, avail);

                float *fobf = new float[channels * avail];
                for (size_t c = 0; c < channels; ++c) {
                    for (int i = 0; i < avail; ++i) {
                        float value = obf[c][i];
                        if (value > 1.f) value = 1.f;
                        if (value < -1.f) value = -1.f;
                        fobf[i * channels + c] = value;
                    }
                }
                //入队，待数据拷贝到audio
                audioData.data = fobf;
                audioData.size = channels * avail;
                //循环播放时_audioDataQueue队列可以一直上涨，直接所有转换完的数据入队，不用睡眠
                _audioDataVec.push_back(audioData);
                if (!_isLoop && _audioDataVec.size() >= MAX_AUDIOQ_SIZE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                for (size_t i = 0; i < channels; ++i) {
                    delete[] obf[i];
                }
                delete[] obf;
            }
        }
        frame += ibs;
    }

    if (isScale) {
        int avail;
        while ((avail = _rubberStrecher->available()) >= 0) {
            std::lock_guard<std::recursive_mutex> lock(_rLock);
            if (_quit) {
                break;
            }
            if (avail > 0) {
                float **obf = new float *[channels];
                for (size_t i = 0; i < channels; ++i) {
                    obf[i] = new float[avail];
                }
                _rubberStrecher->retrieve(obf, avail);
                float *fobf = new float[channels * avail];
                for (size_t c = 0; c < channels; ++c) {
                    for (int i = 0; i < avail; ++i) {
                        float value = obf[c][i];
                        if (value > 1.f) value = 1.f;
                        if (value < -1.f) value = -1.f;
                        fobf[i * channels + c] = value;
                    }
                }

                audioData.data = fobf;
                audioData.size = channels * avail;
                _audioDataVec.push_back(audioData);
                if (!_isLoop && _audioDataVec.size() >= MAX_AUDIOQ_SIZE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                for (size_t i = 0; i < channels; ++i) {
                    delete[] obf[i];
                }
                delete[] obf;
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    _isStrechProcessing = false;
    sf_close(sndfile);
    delete[] fbuf;
    for (size_t i = 0; i < channels; ++i) {
        delete[] ibuf[i];
    }
    delete[] ibuf;
}

void AudioPlayer::pause() {
    if (!_isPlaying) return;
    _isPlaying = false;
    _pauseTick = UtilsWanakaFramework::getUnixTimestamp();
    if (_audioOk) {
        //SDL_LockAudio();
        SDL_PauseAudio(1);
        //SDL_UnlockAudio();
    }
}

void AudioPlayer::stop() {
    if (_quit) {
        return;
    }

    _isPlaying = false;

    if (_audioOk) {
        _quit = true;
        SDL_PauseAudio(1);
        //延迟100ms,让其他线程先退出，为下面的sdl的close，quit正常执行
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        //经过测试这里的close,quit必须在sdl填充数据回调函数退出后才能返回，可能内部实现有锁机制
        //且回调函数的调用函数也使用了这把锁，所以不能让回调进入死循环，而无法释放该锁
        SDL_CloseAudio();
        SDL_Quit();
    }
    reset();
    _delegate->excuteCallback(WanakaAudioPlayer::EventType::Eof);
}

void AudioPlayer::resume() {
    if (_isPlaying) return;
    if (_audioOk) {
        SDL_PauseAudio(0);
        std::lock_guard<std::recursive_mutex> lock(_rLock);
        sStartTimePoint = sCurTimePoint = UtilsWanakaFramework::getUnixTimestamp();
        _totalPauseTime += (sCurTimePoint - _pauseTick);
    }
    _isPlaying = true;
}

void AudioPlayer::seek(float second) {
    if (!_isPlaying) return;
    if (second > _totalTime || second < 0.f) {
        log("seek error !!!");
        return;
    }
    _seekSecond = second;
    _isSeek = true;

    //数据处理完毕
//     if (!_isStrechProcessing) {
//         std::thread seekThread = std::thread(&AudioPlayer::seekProcess, this);
//         seekThread.detach();
//     }

    double destDataIndex = _totalDataSize * (_seekSecond / _totalTime);
    int size = _skipDataSize;
    int index = 0;
    audioDataFrame audioData;
    bool isFind = false;
    std::lock_guard<std::recursive_mutex> lock(_rLock);
    //_elapsedTime = _seekSecond;
    sStartTimePoint = sCurTimePoint = UtilsWanakaFramework::getUnixTimestamp();
    if (!_isStrechProcessing) {
        //应该等于总数据量
        /* for (auto ele : _audioDataVec) {
             size += ele.size;
             }
             size = _skipDataSize;*/

        for (auto ele : _audioDataVec) {
            audioData = ele;
            size += ele.size;
            if (size > destDataIndex) {
                sCurCopyIndex = audioData.size - (size - destDataIndex);
                isFind = true;
                break;
            }
            else if (size == destDataIndex) {
                sCurCopyIndex = size - destDataIndex;
                index++;
                isFind = true;
                break;
            }
            index++;
        }
        if (isFind) {
            sProcessIndex = index;
            sCurCopyIndex *= sizeof(float);
            _isSeek = false;
        }
    }
}

void AudioPlayer::seekProcess() {
    double destDataIndex = _totalDataSize * (_seekSecond / _totalTime);
    int size = 0;
    int index = 0;
    audioDataFrame audioData;
    bool isFind = false;
    while (true) {
        std::lock_guard<std::recursive_mutex> lock(_rLock);
        for (auto ele : _audioDataVec) {
            audioData = ele;
            size += ele.size;
            if (size > destDataIndex) {
                sCurCopyIndex = audioData.size - (size - destDataIndex);
                isFind = true;
                break;
            }
            else if (size == destDataIndex) {
                sCurCopyIndex = size - destDataIndex;
                index++;
                isFind = true;
                break;
            }
            index++;
        }
        if (isFind) {
            sProcessIndex = index;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioPlayer::reset() {
    std::lock_guard<std::recursive_mutex> lock(_rLock);
    _totalTime = 0.f;
    _elapsedTime = 0.f;
    _skipDataSize = 0.f;
    _totalDataSize = 0.f;
    _curPlayIndex = 0.f;
    _curPlayTick = 0.f;
    _pauseTick = 0.f;
    _totalPauseTime = 0.f;
    _isPlaying = false;
    //_quit = false;
    _isLoop = false;
    _audioOk = false;
    _isSeek = false;
    _timeScale = 1.f;
    _seekSecond = 0.f;
    _sdlUseDataInterval = 0.f;
    clearCache();
    if (_rubberStrecher) {
        _rubberStrecher->reset();
        delete _rubberStrecher;
        _rubberStrecher = nullptr;
    }
}

void AudioPlayer::clearCache() {
    for (auto ele : _audioDataVec) {
        delete ele.data;
    }
    _audioDataVec.clear();
}

float AudioPlayer::getDuration() {
    return _totalTime;
}

float AudioPlayer::getCurrent() {

    if (_quit) {
        return 0.0f;
    }

    if (_seekSecond > 0.000001) { //如果在调用时刚seek过，则返回_seekSecond，_seekSecond会在数据切换时被重置
        return _seekSecond;
    }
    _sdlUseDataInterval = sSdlBuffLen / (_totalDataSize * sizeof(float)) * _totalTime;
    double nowTick = UtilsWanakaFramework::getUnixTimestamp();
    if (_isPlaying && _totalDataSize > 0 && _curPlayTick > 0 && _sdlUseDataInterval > 0) {
        double maxElapsed = _elapsedTime + (_sdlUseDataInterval - _elapsedTime) * 0.9; //设置一个最大的流逝点
        double realElapsed = _elapsedTime + (nowTick - _curPlayTick - _totalPauseTime) / _timeScale;
        _elapsedTime = min(maxElapsed, realElapsed);
        _totalPauseTime = 0;
        _curPlayTick = nowTick;
    }
    //每次更新数据在歌曲总长度中的时间位置
    double updateTick = _curPlayIndex / (_totalDataSize * sizeof(float)) * _totalTime;
    double curTime = updateTick + _elapsedTime;
//     log("c++ _elapsedTime = %f, _curPlayIndex = %f, _curPlayTick = %f, _totalDataSize = %f, nowTick = %f, _totalPauseTime = %f, _timeScale = %f, curTime = %f", _elapsedTime,
//         _curPlayIndex, _curPlayTick, _totalDataSize, nowTick, _totalPauseTime, _timeScale, curTime);
    return curTime;
}

void AudioPlayer::setVolume(float volume) {
    //if (!_isPlaying) return;
    if (volume < 0.0000001) volume = 0.f;
    if (volume > 1.f) volume = 1.f;
    _volume = volume;
}

void AudioPlayer::process() {
    if (_isPlaying)
        _delegate->excuteCallback(WanakaAudioPlayer::EventType::Process);
}
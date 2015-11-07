#include "AudioPlayer.h"
#include "UtilsWanakaFramework.h"
#include "WanakaAudioPlayer.h"
#include <Dsound.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <iostream>
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

static AudioPlayer* sInstance = nullptr;

static int sProcessIndex = 0;
static int sCurCopyIndex = 0;
static double sFirstSoundTime = 0.f;
static double sStartTimePoint = 0.f;
static double sCurTimePoint = 0.f;
//======================================================
//sdl调用的音频处理函数
//======================================================
/*static */void processAudio(AudioPlayer* player, uint8_t* stream, int len) {
    int needCopy = 0;
    bool isCopyComplete = false;
	bool isChangeVolume = false;
    while (len > 0) {
        player->_lock.lock();
        if (player->_audioDataVec.size() > sProcessIndex) {
            if (sFirstSoundTime > -0.0000001 && sFirstSoundTime < 0.000001) {
                sFirstSoundTime = UtilsWanakaFramework::getUnixTimestamp();
                sStartTimePoint = sCurTimePoint = sFirstSoundTime;
            }
            audioDataFrame audioData = player->_audioDataVec[sProcessIndex];
			if (player->_volume < 1.f) {
					for (int i = 0; i < audioData.size; i++) {
						audioData.data[i] *= player->_volume;
					}
			}
            int leftByteOfAudioData = audioData.size * sizeof(float) - sCurCopyIndex;
            if (len >= leftByteOfAudioData) {
                needCopy = leftByteOfAudioData;
                isCopyComplete = true;
                sProcessIndex += 1;
            }
            else {
                needCopy = len;
            }
            len -= needCopy;
            memcpy(stream, ((uint8_t*)audioData.data + sCurCopyIndex), needCopy);
			std::cout<<"player->_audioDataVec.size() / sProcessIndex = "<<player->_audioDataVec.size()<< " "<<sProcessIndex << endl;
            stream += needCopy;
            sCurCopyIndex += needCopy;
            if (isCopyComplete) {
                sCurCopyIndex = 0;
                isCopyComplete = false;
				if (player->_audioDataVec.size() == sProcessIndex) {
					player->_lock.unlock();
					return;
				}
            }
        }
        else {
            SDL_Delay(100);
        }
        player->_lock.unlock();
    }
    //needCopy = 0;
}

void sdl_audio_callback(void *userdata, Uint8* stream, int len) {
    AudioPlayer* player = (AudioPlayer*)userdata;
    processAudio(player, stream, len);
}
//======================================================

// AudioPlayer* AudioPlayer::getInstance(){
//     if (!sInstance) {
//         sInstance = new AudioPlayer;
//     }
//     return sInstance;
// }

AudioPlayer::AudioPlayer() :
_totalTime(0.f),
_elapsedTime(0.f),
_isPlaying(false),
_isStrechProcessing(false),
_isSeek(false),
_quit(false),
_isLoop(false),
_audioOk(false),
_volume(1.f),
_timeScale(1.f),
_seekSecond(0.f),
_totalDataSize(0),
_skipDataSize(0),
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

    //SDL_SetMainReady();
//     if (SDL_Init(SDL_INIT_AUDIO) != 0) {
//         log("Could not initialize SDL - %s\n", SDL_GetError());
//         return false;
//     }

    SDL_AudioSpec wanted_spec, spec;
    wanted_spec.freq = sfinfo.samplerate;
    wanted_spec.format = AUDIO_F32SYS;
    wanted_spec.channels = sfinfo.channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    SDL_AudioDeviceID audioID = SDL_OpenAudio(&wanted_spec, &spec);
    if (audioID != 0) {
        log("can't open audio.\n");
        const char * errMsg = SDL_GetError();
        log("err%s", errMsg);
        SDL_CloseAudio();
        audioID = SDL_OpenAudio(&wanted_spec, &spec);
        if (audioID != 0) {
            return false;
        }
    }

    if (spec.channels != wanted_spec.channels) {
        log("not support channel");
        return false;
    }

//     if (spec.format != AUDIO_S32SYS && spec.format != AUDIO_S16SYS) {
//         log("wrong format");
//         return false;
//     }

    return true;
}

bool AudioPlayer::initAudio(const char* audioPath) {
    _fullPath =  FileUtils::getInstance()->fullPathForFilename(audioPath);
    if (!(_fullPath.compare(""))) {
        log("audioPath scale input error!!!");
        return false;
    }

    if (initAudioContext()) {
        _audioOk = true;
    }
    else {
        return false;
    }
    return true;
}

void AudioPlayer::play(const char* audioPath, float timeScale/* = 1.f*/, bool isLoop/* = false*/) {
    if (_isPlaying) {
        stop();
        //std::this_thread::sleep_for(std::chrono::milliseconds(10));
       // _quit = false; //重入时将退出标记置false
    }
    _lock.lock();
    _quit = false; //重入时将退出标记置false
    sProcessIndex = 0;
    sCurCopyIndex = 0;
    sStartTimePoint = sCurTimePoint = sFirstSoundTime = 0.f;
    _lock.unlock();

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
    _totalTime *= timeScale;
    _totalDataSize *= timeScale;

//     auto director = Director::getInstance();
//     Director::getInstance()->getScheduler()->schedule([director, this](float dt) {
//         if (_quit) {
//             director->getScheduler()->unschedule("stecherPlayerCountDown", this);
//         }
//         else if (_isPlaying) {
//             _elapsedTime += dt;
//             if (!sFirstSound)
//             {
//                 time1 += dt;
//             }
//             if (_elapsedTime >= _totalTime) {
//                 stop();
//                 //                 _quit = true;
//                 //                 reset();
//                 director->getScheduler()->unschedule("stecherPlayerCountDown", this);
//             }
//         }
//     }, this, director->getAnimationInterval(), false, "stecherPlayerCountDown");

    //播放时间计算线程
    std::thread timeThread = std::thread(&AudioPlayer::timeCalculate, this);
    timeThread.detach();

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
    double pitchshift = 0.0;
    SNDFILE *sndfile;
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(SF_INFO));
    sndfile = sf_open(_fullPath.c_str(), SFM_READ, &sfinfo);
    if (!sndfile) {
        log("ERROR: Failed to open input file!!!");
        _delegate->excuteCallback(WanakaAudioPlayer::EventType::LoadError);
        return;
    }
    _delegate->excuteCallback(WanakaAudioPlayer::EventType::LoadSuccess);

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
    float *fbuf = new float[channels * ibs];
    float **ibuf = new float *[channels];
    for (size_t i = 0; i < channels; ++i) ibuf[i] = new float[ibs];
    while (frame < sfinfo.frames) {
        _lock.lock();
        if (_quit) {
            _lock.unlock();
            return;
        }

        //处理过程中seek,直接移动到seek的位置进行处理，seek前面的
        if (_isSeek) {
            _rubberStrecher->reset();
            clearCache();
            sProcessIndex = 0;
            sCurCopyIndex = 0;
            int destFrame = _seekSecond / _totalTime * sfinfo.frames;
            if (sf_seek(sndfile, destFrame, SEEK_SET) < 0) break;
            frame = destFrame;
            _skipDataSize = frame * channels * _timeScale;
            _isSeek = false;
        }

        int count = -1;
        if ((count = sf_readf_float(sndfile, fbuf, ibs)) < 0) break;
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
            //_lock.unlock();
            for (size_t i = 0; i < channels; ++i) {
                delete[] obf[i];
            }
            delete[] obf;
        }
        frame += ibs;
        _lock.unlock();
    }

    int avail;
    while ((avail = _rubberStrecher->available()) >= 0) {
        _lock.lock();
        if (_quit) {
            _lock.unlock();
            return;
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

            //_lock.unlock();
            for (size_t i = 0; i < channels; ++i) {
                delete[] obf[i];
            }
            delete[] obf;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        _lock.unlock();
    }
    _isStrechProcessing = false;
    sf_close(sndfile);
}

void AudioPlayer::pause() {
    if (!_isPlaying) return;
    _isPlaying = false;
    if (_audioOk) {
        //SDL_LockAudio();
        SDL_PauseAudio(1);
        //SDL_UnlockAudio();
    }
}

static bool playEnd = false;
void AudioPlayer::stop() {
    if (_quit) return;
    if (_audioOk && playEnd) {
		//_lock.lock();
		_quit = true;
		//_lock.unlock();
        //SDL_LockAudio();
        //SDL_PauseAudio(1);
		string error = SDL_GetError();
        //SDL_CloseAudio();
		SDL_Quit();
        //SDL_UnlockAudio();
        reset();
        _delegate->excuteCallback(WanakaAudioPlayer::EventType::Eof);
    }
}

void AudioPlayer::resume() {
    if (_isPlaying) return;
    if (_audioOk) {
        //SDL_LockAudio();
        SDL_PauseAudio(0);
        //SDL_UnlockAudio();
        _lock.lock();
        sStartTimePoint = sCurTimePoint = UtilsWanakaFramework::getUnixTimestamp();
        _lock.unlock();
    }
    _isPlaying = true;
}

void AudioPlayer::seek(float second) {
    second *= _timeScale;
    if (!_isPlaying) return;
    if (second > _totalTime || second < 0.f) {
        log("seek error !!!");
        return;
    }
    _isSeek = true;
    _seekSecond = second;

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
    _lock.lock();
    _elapsedTime = _seekSecond;
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
    _lock.unlock();
}

void AudioPlayer::seekProcess() {
    double destDataIndex = _totalDataSize * (_seekSecond / _totalTime);
    int size = 0;
    int index = 0;
    audioDataFrame audioData;
    bool isFind = false;
    while (true) {
        _lock.lock();
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
            _lock.unlock();
            break;
        }
        _lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioPlayer::timeCalculate() {
    while (true) {
        if (sFirstSoundTime > 0.f) {
            _lock.lock();
            if (_quit) {
                _lock.unlock();
                return;
            }
            else if (_isPlaying) {
                sCurTimePoint = UtilsWanakaFramework::getUnixTimestamp();
                _elapsedTime += (sCurTimePoint - sStartTimePoint);
                sStartTimePoint = sCurTimePoint;
                if (_elapsedTime >= _totalTime) {
//                     Director::getInstance()->getScheduler()->performFunctionInCocosThread([this] {
// 						//SDL_Quit();
// 						SDL_Delay(100);
//                         stop();
//                     }
//					);
					playEnd = true;
					//SDL_Quit();
                    _lock.unlock();
					stop();
                    return;
                }
            }
            _lock.unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AudioPlayer::reset() {
    _lock.lock();
    _totalTime = 0.f;
    _elapsedTime = 0.f;
    _skipDataSize = 0.f;
    _totalDataSize = 0.f;
    _isPlaying = false;
    //_quit = false;
    _isLoop = false;
    _audioOk = false;
    _isSeek = false;
    _timeScale = 1.f;
    _seekSecond = 0.f;

    //_lock.lock();
    clearCache();
    if (_rubberStrecher) {
        _rubberStrecher->reset();
        delete _rubberStrecher;
        _rubberStrecher = nullptr;
    }
    _lock.unlock();
}

void AudioPlayer::clearCache() {
    for (auto ele : _audioDataVec) {
        delete ele.data;
    }
    _audioDataVec.clear();
}

float AudioPlayer::getDuration() {
    return _totalTime / _timeScale;
}

float AudioPlayer::getCurrent() {
    return _elapsedTime / _timeScale;
}

// void AudioPlayer::setVolume(float v) {
// 	if (v < 0.f) v = 0.f;
// 	if (v > 1.f) v = 1.f;
// 	
// 	//The nVolume parameter must be between 0.0 and 1.0.
// 	//0.0 means mute and 1.0 means 100.
// 	bool bScalar = true;
// 	HRESULT hr=NULL;
// 	bool decibels = false;
// 	bool scalar = false;
// 	double newVolume=v;
// 
// 	CoInitialize(NULL);
// 	IMMDeviceEnumerator *deviceEnumerator = NULL;
// 	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, 
// 		__uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
// 	IMMDevice *defaultDevice = NULL;
// 
// 	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
// 	deviceEnumerator->Release();
// 	deviceEnumerator = NULL;
// 
// 	IAudioEndpointVolume *endpointVolume = NULL;
// 	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), 
// 		CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&endpointVolume);
// 	defaultDevice->Release();
// 	defaultDevice = NULL;
// 
// 	// -------------------------
// 	float currentVolume = 0;
// 	endpointVolume->GetMasterVolumeLevel(&currentVolume);
// 	//printf("Current volume in dB is: %f\n", currentVolume);
// 
// 	hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
// 	//CString strCur=L"";
// 	//strCur.Format(L"%f",currentVolume);
// 	//AfxMessageBox(strCur);
// 
// 	// printf("Current volume as a scalar is: %f\n", currentVolume);
// 	if (bScalar==false)
// 	{
// 		hr = endpointVolume->SetMasterVolumeLevel((float)newVolume, NULL);
// 	}
// 	else if (bScalar==true)
// 	{
// 		hr = endpointVolume->SetMasterVolumeLevelScalar((float)newVolume, NULL);
// 	}
// 	endpointVolume->Release();
// 
// 	CoUninitialize();
// }

void AudioPlayer::setVolume(float v) {
	if (v < 0.f) v = 0.f;
	if (v > 1.f) v = 1.f;
	_volume = v;
}
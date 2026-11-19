#pragma once
#include "media.hpp"
#include "storage.hpp"
#include "spectrum.hpp"
#include "library.hpp"
#include <array>
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>
namespace tabnamp {
enum class State{Idle,Loading,Buffering,Playing,Paused,Seeking,Stopped,Error};
enum class Action{PlayPause,Play,Pause,Stop,Previous,Next,Select,Volume,Mute,Band,Preamp,Bypass,Shuffle,Repeat,Seek,Scan,Add,OpenPlaylist,SavePlaylist,Remove,MoveUp,MoveDown,Clear,Sort,Search,Landscape,Preset,Browse,OrientationMode,Brightness,Balance,Output,Remaining,Visualize,SavePreset,MoveTo,RemoveSelected,SortArtist,SortAlbum,RetryStorage,CancelScan,KeyboardLayout,StorageRemoved,PlayUri};
struct Command{Action action;int value=0;int index=0;std::string text;uint64_t epoch=0;};
struct DirectoryEntry{std::string path,name;bool directory=false;};
struct PcmBlock {std::array<float,1024> samples{};uint32_t rate=0,generation=0;size_t frames=0;};
struct Snapshot {
    State state=State::Idle;Settings settings;MediaInfo media;size_t count=0;int index=0;int64_t position_ms=0,known_duration_ms=0;size_t unknown_durations=0;
    std::string uri,message;bool dirty=false,scan_active=false;uint64_t underruns=0,clips=0,gapless_transitions=0;size_t buffered_frames=0;
    std::array<float,24> spectrum{},peaks{};
};
// Decoder/controller: one worker. UI posts commands and reads copies. Sink is independent.
class Engine {
    std::string root_;std::unique_ptr<Decoder> decoder_;std::unique_ptr<LibraryIndexer> indexer_;
    std::atomic<uint64_t> cancel_epoch_{0};uint64_t operation_epoch_=0;
    std::vector<Track> tracks_;std::vector<DirectoryEntry> directory_;std::string directory_path_;DIR* browse_handle_=nullptr;size_t browse_entries_=0;std::deque<Command> commands_;std::deque<std::string> history_;
    std::vector<PcmBlock> ring_;size_t head_=0,count_=0,buffered_frames_=0;
    mutable std::mutex mutex_;Snapshot snapshot_;
    Settings settings_;MediaInfo media_;State state_=State::Idle;int index_=0;
    uint32_t generation_=1,random_=0x51ad2357;uint64_t frames_=0,underruns_=0,clips_=0;
    std::atomic<bool> visual_{true};Spectrum spectrum_;
    PcmBlock visual_block_;bool visual_ready_=false;uint32_t visual_generation_=0;std::array<float,24> levels_{},peaks_{};Equalizer eq_,previous_eq_;bool transition_=false;
    std::atomic<bool> eof_{false}, output_failed_{false};
    bool summarize_=true;int64_t duration_total_=0;size_t unknown_durations_=0;
    bool dirty_=false,settings_dirty_=false,session_dirty_=false;
    uint64_t clock_now_=0;
    uint64_t last_save_=0,last_session_=0,reconnect_at_=0;int reconnect_attempt_=0;int64_t resume_ms_=0;std::string message_;
    int failure_chain_=0;
    std::unique_ptr<Decoder> prefetched_;std::vector<PcmBlock> prefetched_pcm_;int prefetched_index_=-1;bool prefetch_attempted_=false;size_t outstanding_=0;uint64_t gapless_transitions_=0;
    void prepare_next();bool switch_prefetched();
    void clear_audio();void start(int index);void next(bool previous,bool automatic=false);void apply(const Command& command,uint64_t now);
    void publish();void save_session();void restore_session();
public:
    explicit Engine(std::string root);
    ~Engine(){if(browse_handle_)closedir(browse_handle_);}
    bool post(Command command);
    void initialize();void tick(uint64_t now);
    bool consume(PcmBlock& block);
    void acknowledge(const PcmBlock& block,bool success);
    void fail_output(){output_failed_=true;}
    void analyse();
    Snapshot snapshot()const;
    std::vector<Track> rows(size_t first,size_t count)const;
    std::vector<DirectoryEntry> directory_rows(size_t first,size_t count,size_t& total,std::string& path)const;
    void set_visual(bool enabled);
    uint32_t generation()const;
    std::string root()const{return root_;}
    // Platform implementation must bound remote fetch size/time and validate TLS.
    std::function<bool(const std::string&,std::string&,std::string&,std::string*)> fetch_playlist;
    std::function<bool()> retry_storage;
    std::function<void()> interrupt_io;
    std::function<void()> release_usb;
};
const char* state_name(State state);
}

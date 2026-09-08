#pragma once
#include "Model.h"

namespace c3 {
// Single pending intent, not a backlog. This transport NEVER calls the engine.
// Native dispatch must resolve fresh ownership/phase and attest its ABI again.
// A refused or consumed request is not retried automatically.
class CommandInbox {
    std::mutex mutex_;
    Command pending_{};
    uint64_t generation_{}, highestRequest_{};
    bool occupied_{};
public:
    bool Submit(const Snapshot& authoritative,const Command& command,uint64_t now) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock || !Eligible(authoritative,command,now) ||
           authoritative.generation<generation_) return false;
        if(authoritative.generation>generation_) {
            generation_=authoritative.generation; highestRequest_=0; occupied_=false; pending_={};
        }
        if(command.request<=highestRequest_) return false;
        // Even a full-inbox refusal consumes this ID. A network repeat must not
        // become a second toggle once space opens up later.
        highestRequest_=command.request;
        if(occupied_) return false;
        pending_=command; occupied_=true; return true;
    }
    bool Take(const Snapshot& authoritative,uint64_t now,Command& out) {
        out={};
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock || !occupied_) return false;
        const Command candidate=pending_;
        pending_={}; occupied_=false;
        if(authoritative.generation<generation_ || !Eligible(authoritative,candidate,now)) return false;
        out=candidate; return true;
    }
    bool Cancel() {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock) return false;
        pending_={}; occupied_=false;
        // Retain the high-water mark: cancelling must not authorize replay.
        return true;
    }
};
} // namespace c3

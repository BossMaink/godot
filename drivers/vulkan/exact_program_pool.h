#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

// Fast keys choose a bucket only. Equality must verify the complete input.
// The index never prolongs the lifetime of a parsed program.
template <class T> class ExactProgramPool {
    struct State {
        std::mutex mutex;
        std::unordered_multimap<std::string,std::weak_ptr<const T>> entries;
    };
    std::shared_ptr<State> state=std::make_shared<State>();
    template<class Equal> static std::shared_ptr<const T> find(State &s,const std::string &key,Equal equal,
            std::vector<std::shared_ptr<const T>> &held) {
        auto range=s.entries.equal_range(key);
        // Allocate before locking any weak owner. All temporary owners, including
        // non-matching collision entries, must be destroyed after unlocking.
        held.reserve(held.size()+s.entries.count(key));
        for(auto it=range.first;it!=range.second;++it) {
            held.push_back(it->second.lock());
            const auto &p=held.back();
            if(p && equal(*p))return p;
        }
        return {};
    }
public:
    // equal is a read-only comparison, invoked under the index lock.
    template<class Factory,class Equal> std::shared_ptr<const T> acquire(const std::string &key,Factory factory,Equal equal) {
        std::vector<std::shared_ptr<const T>> held;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto existing=find(*state,key,equal,held);
            if(existing)return existing;
        }
        held.clear();
        std::unique_ptr<T> parsed(factory());
        auto weak_state=std::weak_ptr<State>(state);
        std::shared_ptr<const T> candidate(parsed.release(),[weak_state,key](const T *p) {
            delete p;
            if(auto s=weak_state.lock()) {
                std::lock_guard<std::mutex> lock(s->mutex);
                auto range=s->entries.equal_range(key);
                for(auto it=range.first;it!=range.second;) {
                    if(it->second.expired())it=s->entries.erase(it);else ++it;
                }
            }
        });
        std::shared_ptr<const T> result;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            result=find(*state,key,equal,held);
            if(!result){state->entries.emplace(key,candidate);result=candidate;}
        }
        // In a first-miss race, destroy the losing parse after unlocking.
        return result;
    }
    size_t indexed_count() const {
        std::lock_guard<std::mutex> lock(state->mutex);
        return state->entries.size();
    }
};

// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {
    curr_size_ = 0;
    mru_target_size_ = 0;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
    std::lock_guard<std::mutex> lock(latch_);
    if (curr_size_ == 0) {
        return std::nullopt;
    }
    
    if (mru_.size() >= mru_target_size_) {
        // try mru first
        for (auto it = mru_.rbegin(); it != mru_.rend(); ++it) { //rbegin is reverse iterator starting at last element and rend is reverse iterator staring at first element
            auto map_it = alive_map_.find(*it);
            if (map_it != alive_map_.end() && map_it->second->evictable_) { //check if evictable
                frame_id_t fid = *it; //derefence the iterator to get fid
                auto obj = map_it->second;
                mru_.erase(std::next(it).base()); // cant use erase directly on reverse iterator so convert it to normal first then erase
                if (mru_ghost_.size() + mfu_ghost_.size() >= replacer_size_) {
                    if (!mru_ghost_.empty()) {
                        ghost_map_.erase(mru_ghost_.back());
                        mru_ghost_.pop_back();
                    }
                    else if (!mfu_ghost_.empty())
                    {
                        ghost_map_.erase(mfu_ghost_.back());
                        mfu_ghost_.pop_back();
                    }
                    
                }
                mru_ghost_.push_front(obj->page_id_);
                obj->arc_status_ = ArcStatus::MRU_GHOST;
                obj->list_it_ = mru_ghost_.begin(); // bring iterator to correct position
                ghost_map_[obj->page_id_] = obj;
                obj->evictable_ = false;
                alive_map_.erase(fid); // remove from alive map
                curr_size_--; // no of evictable frames decrement as space has freed up
                return fid;
            }
        }
        // mru all pinned, fallback to mfu
        for (auto it = mfu_.rbegin(); it != mfu_.rend(); ++it) { //rbegin is reverse iterator starting at last element and rend is reverse iterator staring at first element
            auto map_it = alive_map_.find(*it);
            if (map_it != alive_map_.end() && map_it->second->evictable_) { //check if evictable
                frame_id_t fid = *it; //derefence the iterator to get fid
                auto obj = map_it->second;
                mfu_.erase(std::next(it).base()); // cant use erase directly on reverse iterator so convert it to normal first then erase
                if (mru_ghost_.size() + mfu_ghost_.size() >= replacer_size_) {
                    if (!mfu_ghost_.empty()) {
                        ghost_map_.erase(mfu_ghost_.back());
                        mfu_ghost_.pop_back();
                    }
                    else if(!mru_ghost_.empty()) {
                        ghost_map_.erase(mru_ghost_.back());
                        mru_ghost_.pop_back();
                }
            }
                mfu_ghost_.push_front(obj->page_id_);
                obj->arc_status_ = ArcStatus::MFU_GHOST;
                obj->list_it_ = mfu_ghost_.begin(); // bring iterator to correct position
                ghost_map_[obj->page_id_] = obj;
                obj->evictable_ = false;
                alive_map_.erase(fid); // remove from alive map
                curr_size_--; // no of evictable frames decrement as space has freed up
                return fid;
            }
        }
    } 
    else {
        // try mfu first
        for (auto it = mfu_.rbegin(); it != mfu_.rend(); ++it) { //rbegin is reverse iterator starting at last element and rend is reverse iterator staring at first element
            auto map_it = alive_map_.find(*it);
            if (map_it != alive_map_.end() && map_it->second->evictable_) { //check if evictable
                frame_id_t fid = *it; //derefence the iterator to get fid
                auto obj = map_it->second;
                mfu_.erase(std::next(it).base()); //same as before
                if (mru_ghost_.size() + mfu_ghost_.size() >= replacer_size_) {
                    if (!mfu_ghost_.empty()) {
                        ghost_map_.erase(mfu_ghost_.back());
                        mfu_ghost_.pop_back();
                    }
                    else if (!mru_ghost_.empty()) {
                        ghost_map_.erase(mru_ghost_.back());
                        mru_ghost_.pop_back();
                    }
                }
                mfu_ghost_.push_front(obj->page_id_);
                obj->arc_status_ = ArcStatus::MFU_GHOST;
                obj->list_it_ = mfu_ghost_.begin(); //same as before
                ghost_map_[obj->page_id_] = obj;
                obj->evictable_ = false;
                alive_map_.erase(fid);  // same as before
                curr_size_--; //same as before
                return fid;
            }
        }
        // MFU all pinned, fallback to MRU
        for (auto it = mru_.rbegin(); it != mru_.rend(); ++it) { //rbegin is reverse iterator starting at last element and rend is reverse iterator staring at first element
            auto map_it = alive_map_.find(*it);
            if (map_it != alive_map_.end() && map_it->second->evictable_) { //check if evictable
                frame_id_t fid = *it; //same as before
                auto obj = map_it->second; 
                mru_.erase(std::next(it).base()); //same as before
                if (mru_ghost_.size() + mfu_ghost_.size() >= replacer_size_) {
                    if (!mru_ghost_.empty()) {
                        ghost_map_.erase(mru_ghost_.back());
                        mru_ghost_.pop_back();
                    }
                    else if (!mfu_ghost_.empty()) {
                        ghost_map_.erase(mfu_ghost_.back());
                        mfu_ghost_.pop_back();
                    }
                }
                mru_ghost_.push_front(obj->page_id_);
                obj->arc_status_ = ArcStatus::MRU_GHOST;
                obj->list_it_ = mru_ghost_.begin(); //same as before
                ghost_map_[obj->page_id_] = obj;
                obj->evictable_ = false;
                alive_map_.erase(fid);  // same as before
                curr_size_--;  // same as before
                return fid;
            }
        }
    }

    return std::nullopt;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
std::lock_guard<std::mutex> lock(latch_);
if(alive_map_.count(frame_id)) // if present in mfu or mru logic
{
    auto obj = alive_map_[frame_id];
    if(obj->arc_status_ == ArcStatus::MRU) //in mru
    {
        mru_.erase(obj->list_it_); //find by iterator which knows exactly where its located and erase it
        mfu_.push_front(frame_id);
        
        obj->arc_status_ = ArcStatus::MFU;
        obj->list_it_ = mfu_.begin(); // make iterator point to the element just inserted
    }
    else if(obj->arc_status_ == ArcStatus::MFU) // in mfu
    {
        mfu_.erase(obj->list_it_); //same as before
        mfu_.push_front(frame_id);

        obj->list_it_ = mfu_.begin(); // same as before
    }
}
else{ //if not present in mfu or mru
    if(ghost_map_.count(page_id)) // if present in ghost mfu or mru
    {
        auto obj = ghost_map_[page_id];
        if(obj->arc_status_== ArcStatus::MRU_GHOST) // in mru ghost
        {
            if(mru_ghost_.size()>=mfu_ghost_.size() )
            {
                mru_target_size_ = std::min(++mru_target_size_,replacer_size_); // target size should not be lower than replacer size
                mru_ghost_.erase(obj->list_it_); //remove page using iterator
                mfu_.push_front(frame_id);
                obj->list_it_=mfu_.begin(); // update element location
                obj->arc_status_ = ArcStatus::MFU;
                alive_map_[frame_id] = obj; //transfer element to alive map
                ghost_map_.erase(page_id); //delete element from ghost map
            }
            else
            {
                int x=0;
            if(mru_ghost_.size()!=0)
            {
               x= mfu_ghost_.size()/mru_ghost_.size();
            }
               mru_target_size_ = std::min(mru_target_size_ + x , replacer_size_); // if replacer is smaller than mru target
               mru_ghost_.erase(obj->list_it_); // same as before
               mfu_.push_front(frame_id);
               obj->list_it_ = mfu_.begin(); // same as before
               obj->arc_status_ = ArcStatus::MFU;
                alive_map_[frame_id] = obj; //transfer element to alive map
                ghost_map_.erase(page_id); //delete element from ghost map

            }
        }
        else if(obj->arc_status_==ArcStatus::MFU_GHOST) // in mfu ghost
        {
           
            if(mfu_ghost_.size()>=mru_ghost_.size())
            {
                if (mru_target_size_ > 0) {
                    mru_target_size_--;
                }
                mfu_ghost_.erase(obj->list_it_); //same
                mfu_.push_front(frame_id);
                obj->list_it_ = mfu_.begin(); //same
                obj->arc_status_ = ArcStatus::MFU;
                alive_map_[frame_id] = obj; //transfer element to alive map
                ghost_map_.erase(page_id); //delete element from ghost map
            }
            else
            {
                uint32_t x=0;
                if(mfu_ghost_.size()!=0)
                {
                 x= mru_ghost_.size()/mfu_ghost_.size();
                 if(mru_ghost_.size()>=x)
                 {
                    mru_target_size_= mru_target_size_-x;
                 }
                 else
                 {
                    mru_target_size_=0;
                 }
                 
                }
                mfu_ghost_.erase(obj->list_it_); //same
                mfu_.push_front(frame_id);
                obj->arc_status_ = ArcStatus::MFU; //same
                obj->list_it_ = mfu_.begin();
                alive_map_[frame_id] = obj; //transfer element to alive map
                ghost_map_.erase(page_id); //delete element from ghost map
                
            }       
           
        }

    } 
    else // if not present anywhere
    {
      if(mru_.size() +mru_ghost_.size() == replacer_size_) //mru + mrughost = replacer
      {
        ghost_map_.erase(mru_ghost_.back());
        mru_ghost_.pop_back();
        mru_.push_front(frame_id);
        auto obj = std::make_shared<FrameStatus>(page_id,frame_id,false,ArcStatus::MRU, mru_.begin()); // make obj with same struct as the alive map to store
        alive_map_[frame_id]=obj;
      }
      else if (mru_.size() + mru_ghost_.size() < replacer_size_) // mru + mrughost < replacer
      {
        if(mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() == 2*replacer_size_)
        {
            ghost_map_.erase(mfu_ghost_.back());
            mfu_ghost_.pop_back();
            mru_.push_front(frame_id);
            auto obj = std::make_shared<FrameStatus>(page_id,frame_id,false,ArcStatus::MRU, mru_.begin()); // make obj with same struct as the alive map to store
            alive_map_[frame_id]=obj;
        }
        else
        {
            mru_.push_front(frame_id);
            auto obj = std::make_shared<FrameStatus>(page_id,frame_id,false,ArcStatus::MRU, mru_.begin()); // make obj with same struct as the alive map to store
            alive_map_[frame_id]=obj;
        }
      }     
    }
}


}

/** 
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
    std::lock_guard<std::mutex> lock(latch_);
    if(frame_id<0 ) // as given in config.h as invalid state
    {
        throw std::invalid_argument("Invalid frame_id"); // throw exception if frame id invalid
    }
    if(alive_map_.count(frame_id)) // check if the frame exists
    {
        auto obj = alive_map_[frame_id];
        if( obj->evictable_== 0 && set_evictable == true) // if not evictable and want to set to evictable
        {
            curr_size_++; //current size represents current no of evictable frames
            obj->evictable_ = 1;
        }
        else if(obj->evictable_ == 1 && set_evictable == false) // if evictable and want to set to not evictable
        {
            curr_size_--;
            obj->evictable_= 0;
        }
    }
    // for every other case just return
    //evictable and want to set to evictable
    //not evictable and want to set to not evictable


}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if(alive_map_.count(frame_id)) // if specified frame is found
    {
        auto obj = alive_map_[frame_id];
        if(obj->evictable_ == 0) // if frame is not evictable
        {
            throw std::invalid_argument("Frame not evictable");
        }
        else // if frame is evictable
        { 
            if(obj->arc_status_ == ArcStatus::MFU) // if in mfu
            {            
            mfu_.erase(obj->list_it_); // object selected through iterator and then obj is removed
            alive_map_.erase(frame_id);
            curr_size_--;
            }
            else if (obj->arc_status_ == ArcStatus::MRU) // if in mru
            {
                mru_.erase(obj->list_it_); // object selected through iterator and then obj is removed
                alive_map_.erase(frame_id);
                curr_size_--;
            }
            else{ // if not in mfu or mru
                // do nothing
            }
            
            
        }
    }
    else // if specified frame is not found
    return;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
    std::lock_guard<std::mutex> lock(latch_);
    return curr_size_; } // return evictable frames

}  // namespace bustub

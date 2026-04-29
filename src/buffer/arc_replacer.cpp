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
#include <algorithm>
#include <optional>
#include <stdexcept>
#include "common/config.h"

namespace bustub {

ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);

  // If no frames are currently evictable, we can't do anything.
  if (curr_size_ == 0) {
    return std::nullopt;
  }

  // Determine which list we ideally want to evict from based on target size (p)
  bool prefer_mru = (mru_.size() >= mru_target_size_);

  // Helper lambda to attempt eviction from a specific list
  auto try_evict_from = [&](std::list<frame_id_t> &active_list, std::list<page_id_t> &ghost_list,
                            ArcStatus ghost_status) -> std::optional<frame_id_t> {
    // Iterate from the back (LRU side) to find the first evictable frame
    for (auto it = active_list.rbegin(); it != active_list.rend(); ++it) {
      frame_id_t frame_id = *it;
      auto status_it = alive_map_.find(frame_id);
      
      if (status_it != alive_map_.end() && status_it->second->evictable_) {
        auto frame_status = status_it->second;
        page_id_t page_id = frame_status->page_id_;

        // 1. Remove from active list, push to ghost list
        active_list.remove(frame_id);
        ghost_list.push_front(page_id);

        // 2. Update status and move between maps
        frame_status->arc_status_ = ghost_status;
        frame_status->evictable_ = false; // Ghosts are not actively evictable
        ghost_map_[page_id] = frame_status;
        alive_map_.erase(frame_id);
        curr_size_--;

        // 3. Maintain ghost list capacity (C) to prevent infinite growth
        if (ghost_list.size() > replacer_size_) {
          page_id_t oldest_ghost = ghost_list.back();
          ghost_list.pop_back();
          ghost_map_.erase(oldest_ghost);
        }

        return frame_id;
      }
    }
    return std::nullopt;
  };

  std::optional<frame_id_t> victim = std::nullopt;

  // Execute the balancing policy with the fallback required by the project instructions
  if (prefer_mru) {
    victim = try_evict_from(mru_, mru_ghost_, ArcStatus::MRU_GHOST);
    if (!victim) { victim = try_evict_from(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST); }
  } else {
    victim = try_evict_from(mfu_, mfu_ghost_, ArcStatus::MFU_GHOST);
    if (!victim) { victim = try_evict_from(mru_, mru_ghost_, ArcStatus::MRU_GHOST); }
  }

  return victim;
}

void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);

  // CASE 1: Cache Hit in Active Lists
  auto alive_it = alive_map_.find(frame_id);
  if (alive_it != alive_map_.end()) {
    auto frame_status = alive_it->second;
    if (frame_status->arc_status_ == ArcStatus::MRU) {
      mru_.remove(frame_id);
    } else {
      mfu_.remove(frame_id);
    }
    mfu_.push_front(frame_id);
    frame_status->arc_status_ = ArcStatus::MFU;
    return;
  }

  bool default_evictable = false;

  // CASE 2/3: Cache Hit in Ghost Lists
  auto ghost_it = ghost_map_.find(page_id);
  if (ghost_it != ghost_map_.end()) {
    auto ghost_status = ghost_it->second;
    
    if (ghost_status->arc_status_ == ArcStatus::MRU_GHOST) {
      size_t step = (mru_ghost_.size() >= mfu_ghost_.size()) ? 1 : (mfu_ghost_.size() / mru_ghost_.size());
      mru_target_size_ = std::min(replacer_size_, mru_target_size_ + step);
      mru_ghost_.remove(page_id);
    } else {
      size_t step = (mfu_ghost_.size() >= mru_ghost_.size()) ? 1 : (mru_ghost_.size() / mfu_ghost_.size());
      mru_target_size_ = (mru_target_size_ > step) ? (mru_target_size_ - step) : 0;
      mfu_ghost_.remove(page_id);
    }

    ghost_map_.erase(ghost_it);
    mfu_.push_front(frame_id);
    alive_map_[frame_id] = std::make_shared<FrameStatus>(page_id, frame_id, default_evictable, ArcStatus::MFU);
    return;
  }

  // CASE 4: Complete Cache Miss
  mru_.push_front(frame_id);
  alive_map_[frame_id] = std::make_shared<FrameStatus>(page_id, frame_id, default_evictable, ArcStatus::MRU);
}

void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);

  if (frame_id >= replacer_size_) {
    throw std::invalid_argument("Invalid frame_id: Exceeds replacer capacity.");
  }

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  auto frame_status = it->second;
  if (frame_status->evictable_ && !set_evictable) {
    curr_size_--;
  } else if (!frame_status->evictable_ && set_evictable) {
    curr_size_++;
  }

  frame_status->evictable_ = set_evictable;
}

void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) {
    return;
  }

  auto frame_status = it->second;
  if (!frame_status->evictable_) {
    throw std::logic_error("Cannot remove a non-evictable frame.");
  }

  if (frame_status->arc_status_ == ArcStatus::MRU) {
    mru_.remove(frame_id);
  } else if (frame_status->arc_status_ == ArcStatus::MFU) {
    mfu_.remove(frame_id);
  }

  alive_map_.erase(it);
  curr_size_--;
}

auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
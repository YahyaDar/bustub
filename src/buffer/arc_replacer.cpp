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
#include <iterator>
#include <stdexcept>

#include "common/config.h"

namespace bustub {

ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

auto ArcReplacer::GetVictim() -> AliveIter {
  auto find_victim = [](AliveList &list) -> AliveIter {
    for (auto rit = list.rbegin(); rit != list.rend(); ++rit) {
      if ((*rit)->evictable_) {
        return std::prev(rit.base());
      }
    }
    return list.end();
  };

  if (mru_.size() >= mru_target_size_) {
    auto victim = find_victim(mru_);
    if (victim != mru_.end()) {
      return victim;
    }
    victim = find_victim(mfu_);
    if (victim != mfu_.end()) {
      return victim;
    }
  } else {
    auto victim = find_victim(mfu_);
    if (victim != mfu_.end()) {
      return victim;
    }
    victim = find_victim(mru_);
    if (victim != mru_.end()) {
      return victim;
    }
  }

  throw std::logic_error("No evictable frame found");
}

auto ArcReplacer::MoveVictimToGhost(AliveIter victim_it) -> frame_id_t {
  auto victim = *victim_it;
  const auto frame_id = victim->frame_id_;

  alive_map_.erase(frame_id);

  if (victim->arc_status_ == ArcStatus::MRU) {
    mru_.erase(victim_it);
    victim->arc_status_ = ArcStatus::MRU_GHOST;
    mru_ghost_.push_front(victim);
    ghost_map_[victim->page_id_] = mru_ghost_.begin();
  } else {
    mfu_.erase(victim_it);
    victim->arc_status_ = ArcStatus::MFU_GHOST;
    mfu_ghost_.push_front(victim);
    ghost_map_[victim->page_id_] = mfu_ghost_.begin();
  }

  victim->evictable_ = false;
  return frame_id;
}

void ArcReplacer::IncreaseTargetSize(int64_t delta) {
  if (delta >= 0) {
    const auto increase = static_cast<size_t>(delta);
    if (increase > replacer_size_ - mru_target_size_) {
      mru_target_size_ = replacer_size_;
      return;
    }
    mru_target_size_ += increase;
    return;
  }

  const auto decrease = static_cast<size_t>(-delta);
  if (decrease >= mru_target_size_) {
    mru_target_size_ = 0;
    return;
  }

  mru_target_size_ -= decrease;
}

void ArcReplacer::RecordAccessAlive(frame_id_t frame_id, page_id_t page_id) {
  auto map_it = alive_map_.find(frame_id);
  if (map_it == alive_map_.end()) {
    throw std::runtime_error("frame not found");
  }

  auto list_it = map_it->second;
  auto record = *list_it;
  BUSTUB_ASSERT(record->page_id_ == page_id, "page id mismatch");

  if (record->arc_status_ == ArcStatus::MRU) {
    mru_.erase(list_it);
  } else {
    mfu_.erase(list_it);
  }

  record->arc_status_ = ArcStatus::MFU;
  mfu_.push_front(record);
  alive_map_[frame_id] = mfu_.begin();
}

void ArcReplacer::RecordAccessGhost(frame_id_t frame_id, page_id_t page_id) {
  auto map_it = ghost_map_.find(page_id);
  if (map_it == ghost_map_.end()) {
    return;
  }

  auto list_it = map_it->second;
  auto record = *list_it;

  if (record->arc_status_ == ArcStatus::MRU_GHOST) {
    IncreaseTargetSize(1);
    mru_ghost_.erase(list_it);
  } else {
    IncreaseTargetSize(-1);
    mfu_ghost_.erase(list_it);
  }

  ghost_map_.erase(map_it);

  record->frame_id_ = frame_id;
  record->page_id_ = page_id;
  record->evictable_ = false;
  record->arc_status_ = ArcStatus::MFU;
  mfu_.push_front(record);
  alive_map_[frame_id] = mfu_.begin();
}

void ArcReplacer::RecordAccessNew(frame_id_t frame_id, page_id_t page_id) {
  if (mru_.size() + mru_ghost_.size() == replacer_size_) {
    if (mru_ghost_.empty()) {
      return;
    }
    ghost_map_.erase(mru_ghost_.back()->page_id_);
    mru_ghost_.pop_back();
  } else if (mru_.size() + mru_ghost_.size() + mfu_.size() + mfu_ghost_.size() >= 2 * replacer_size_) {
    if (mfu_ghost_.empty()) {
      return;
    }
    ghost_map_.erase(mfu_ghost_.back()->page_id_);
    mfu_ghost_.pop_back();
  }

  auto record = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
  mru_.push_front(record);
  alive_map_[frame_id] = mru_.begin();
}

/**
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);

  if (curr_size_ == 0) {
    return std::nullopt;
  }

  auto victim_it = GetVictim();
  curr_size_--;
  return MoveVictimToGhost(victim_it);
}

void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  std::lock_guard<std::mutex> lock(latch_);

  if (alive_map_.find(frame_id) != alive_map_.end()) {
    RecordAccessAlive(frame_id, page_id);
    return;
  }

  if (ghost_map_.find(page_id) != ghost_map_.end()) {
    RecordAccessGhost(frame_id, page_id);
    return;
  }

  RecordAccessNew(frame_id, page_id);
}

void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);

  auto map_it = alive_map_.find(frame_id);
  if (map_it == alive_map_.end()) {
    throw std::runtime_error("frame not found");
  }

  auto record = *map_it->second;
  if (record->evictable_ == set_evictable) {
    return;
  }

  record->evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
}

void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto map_it = alive_map_.find(frame_id);
  if (map_it == alive_map_.end()) {
    return;
  }

  auto record = *map_it->second;
  if (!record->evictable_) {
    throw std::runtime_error("frame not evictable");
  }

  if (record->arc_status_ == ArcStatus::MRU) {
    mru_.erase(map_it->second);
  } else {
    mfu_.erase(map_it->second);
  }

  alive_map_.erase(map_it);
  curr_size_--;
}

auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
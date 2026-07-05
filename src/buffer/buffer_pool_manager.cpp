//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "common/macros.h"

namespace bustub {

FrameHeader::FrameHeader(frame_id_t frame_id) : frame_id_(frame_id) {
  pin_count_ = 0;
  is_dirty_ = false;
  data_.resize(BUSTUB_PAGE_SIZE, 0); 
  Reset(); 
}

auto FrameHeader::GetData() const -> const char * { return data_.data(); }

auto FrameHeader::GetDataMut() -> char * { return data_.data(); }

void FrameHeader::Reset() {
  std::fill(data_.begin(), data_.end(), 0);
  pin_count_.store(0);
  is_dirty_ = false;
}

BufferPoolManager::BufferPoolManager(size_t num_frames, DiskManager *disk_manager, LogManager *log_manager)
    : num_frames_(num_frames),
      next_page_id_(0),
      bpm_latch_(std::make_shared<std::mutex>()),
      replacer_(std::make_shared<ArcReplacer>(num_frames)),
      disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)),
      log_manager_(log_manager) {
  
  bpm_latch_->lock();

  next_page_id_.store(0);
  frames_.reserve(num_frames_);
  page_table_.reserve(num_frames_);

  for (size_t i = 0; i < num_frames_; i++) {
    frames_.push_back(std::make_shared<FrameHeader>(i));
    free_frames_.push_back(static_cast<int>(i));
  }

  bpm_latch_->unlock();
}

BufferPoolManager::~BufferPoolManager() = default;

auto BufferPoolManager::FindFreeFrame(frame_id_t *out_frame_id) -> bool {
  if (!free_frames_.empty()) {
    *out_frame_id = free_frames_.front();
    free_frames_.pop_front();
    return true;
  }

  std::optional<frame_id_t> evicted_frame_opt = replacer_->Evict();
  
  if (evicted_frame_opt.has_value()) {
    *out_frame_id = evicted_frame_opt.value();
    auto &frame = frames_[*out_frame_id];

    if (frame->is_dirty_) {
      std::promise<bool> promise;
      std::future<bool> future = promise.get_future();
      
      DiskRequest req{true, frame->GetDataMut(), frame->page_id_, std::move(promise)};
      std::vector<DiskRequest> scheduling_queue;
      scheduling_queue.push_back(std::move(req));
      
      disk_scheduler_->Schedule(scheduling_queue);
      future.get();
      
      frame->is_dirty_ = false; 
    }

    if (frame->page_id_ != INVALID_PAGE_ID) {
      page_table_.erase(frame->page_id_);
    }
    
    frame->Reset(); 
    frame->page_id_ = INVALID_PAGE_ID; 
    
    return true;
  }

  return false;
}

auto BufferPoolManager::Size() const -> size_t { return num_frames_; }

auto BufferPoolManager::NewPage() -> page_id_t {
  bpm_latch_->lock();
  page_id_t new_id = next_page_id_.fetch_add(1);
  bpm_latch_->unlock();
  return new_id;
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  bpm_latch_->lock();

  auto it = page_table_.find(page_id);

  if (it == page_table_.end()) {
    disk_scheduler_->DeallocatePage(page_id);
    bpm_latch_->unlock();
    return true;
  }

  frame_id_t frame_id = it->second;
  auto &frame = frames_[frame_id];

  if (frame->pin_count_ > 0) {
    bpm_latch_->unlock();
    return false;
  }

  page_table_.erase(page_id);
  replacer_->Remove(frame_id);

  frame->page_id_ = INVALID_PAGE_ID;
  frame->is_dirty_ = false;
  frame->pin_count_ = 0;

  free_frames_.push_back(frame_id);
  disk_scheduler_->DeallocatePage(page_id);

  bpm_latch_->unlock();
  return true;
}

auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
  bpm_latch_->lock();
  auto it = page_table_.find(page_id);

  frame_id_t target_frame_id;

  if (it == page_table_.end()) {
    bool frame_was_found = FindFreeFrame(&target_frame_id);
    if (!frame_was_found) {
      bpm_latch_->unlock();
      return std::nullopt; 
    }

    char* ptr_to_acquired_frame = frames_[target_frame_id]->GetDataMut();

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();

    DiskRequest req{false, ptr_to_acquired_frame, page_id, std::move(promise)};
    std::vector<DiskRequest> scheduling_queue;
    scheduling_queue.push_back(std::move(req));
    disk_scheduler_->Schedule(scheduling_queue);

    future.get();
    page_table_[page_id] = target_frame_id;
  } else {
    target_frame_id = it->second;
  }

  auto &frame = frames_[target_frame_id];
  frame->page_id_ = page_id;
  frame->pin_count_++;
  
  replacer_->RecordAccess(target_frame_id, page_id);
  replacer_->SetEvictable(target_frame_id, false);
  
  // DEADLOCK FIX: Unlock global latch BEFORE guard creation
  bpm_latch_->unlock();
  
  return WritePageGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
}

auto BufferPoolManager::CheckedReadPage(page_id_t page_id, AccessType access_type) -> std::optional<ReadPageGuard> {
  bpm_latch_->lock();
  auto it = page_table_.find(page_id);

  frame_id_t target_frame_id;

  if (it == page_table_.end()) {
    bool frame_was_found = FindFreeFrame(&target_frame_id);
    if (!frame_was_found) {
      bpm_latch_->unlock();
      return std::nullopt; 
    }

    char* ptr_to_acquired_frame = frames_[target_frame_id]->GetDataMut();

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();

    DiskRequest req{false, ptr_to_acquired_frame, page_id, std::move(promise)};
    std::vector<DiskRequest> scheduling_queue;
    scheduling_queue.push_back(std::move(req));
    disk_scheduler_->Schedule(scheduling_queue);

    future.get();
    page_table_[page_id] = target_frame_id;
  } else {
    target_frame_id = it->second;
  }

  auto &frame = frames_[target_frame_id];
  frame->page_id_ = page_id;
  frame->pin_count_++;
  
  replacer_->RecordAccess(target_frame_id, page_id);
  replacer_->SetEvictable(target_frame_id, false);
  
  // DEADLOCK FIX: Unlock global latch BEFORE guard creation
  bpm_latch_->unlock();
  
  return ReadPageGuard(page_id, frame, replacer_, bpm_latch_, disk_scheduler_);
}

auto BufferPoolManager::WritePage(page_id_t page_id, AccessType access_type) -> WritePageGuard {
  auto guard_opt = CheckedWritePage(page_id, access_type);
  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
    std::abort();
  }
  return std::move(guard_opt).value();
}

auto BufferPoolManager::ReadPage(page_id_t page_id, AccessType access_type) -> ReadPageGuard {
  auto guard_opt = CheckedReadPage(page_id, access_type);
  if (!guard_opt.has_value()) {
    fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
    std::abort();
  }
  return std::move(guard_opt).value();
}

auto BufferPoolManager::FlushPageUnsafe(page_id_t page_id) -> bool {
  bpm_latch_->lock();

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    bpm_latch_->unlock();
    return false;
  }

  frame_id_t frame_id = it->second;
  auto &frame = frames_[frame_id];

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  DiskRequest req{true, frame->GetDataMut(), page_id, std::move(promise)};
  std::vector<DiskRequest> scheduling_queue;
  scheduling_queue.push_back(std::move(req));

  frame->is_dirty_ = false;

  disk_scheduler_->Schedule(scheduling_queue);
  future.get();

  bpm_latch_->unlock();
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  bpm_latch_->lock();

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    bpm_latch_->unlock();
    return false;
  }

  frame_id_t frame_id = it->second;
  auto &frame = frames_[frame_id];

  frame->rwlatch_.lock_shared();

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  DiskRequest req{true, frame->GetDataMut(), page_id, std::move(promise)};
  std::vector<DiskRequest> scheduling_queue;
  scheduling_queue.push_back(std::move(req));

  frame->is_dirty_ = false;

  disk_scheduler_->Schedule(scheduling_queue);
  future.get();

  frame->rwlatch_.unlock_shared();

  bpm_latch_->unlock();
  return true;
}

void BufferPoolManager::FlushAllPagesUnsafe() {
  bpm_latch_->lock();

  for (const auto &[page_id, frame_id] : page_table_) {
    auto &frame = frames_[frame_id];

    if (frame->is_dirty_) {
      std::promise<bool> promise;
      std::future<bool> future = promise.get_future();

      DiskRequest req{true, frame->GetDataMut(), page_id, std::move(promise)};
      std::vector<DiskRequest> scheduling_queue;
      scheduling_queue.push_back(std::move(req));

      frame->is_dirty_ = false;

      disk_scheduler_->Schedule(scheduling_queue);
      future.get();
    }
  }
  
  bpm_latch_->unlock();
}

void BufferPoolManager::FlushAllPages() {
  bpm_latch_->lock();

  for (const auto &[page_id, frame_id] : page_table_) {
    auto &frame = frames_[frame_id];

    if (frame->is_dirty_) {
      frame->rwlatch_.lock_shared();

      std::promise<bool> promise;
      std::future<bool> future = promise.get_future();

      DiskRequest req{true, frame->GetDataMut(), page_id, std::move(promise)};
      std::vector<DiskRequest> scheduling_queue;
      scheduling_queue.push_back(std::move(req));

      frame->is_dirty_ = false;

      disk_scheduler_->Schedule(scheduling_queue);
      future.get();

      frame->rwlatch_.unlock_shared();
    }
  }
  
  bpm_latch_->unlock();
}

auto BufferPoolManager::GetPinCount(page_id_t page_id) -> std::optional<size_t> {
  bpm_latch_->lock();
  
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    bpm_latch_->unlock();
    return std::nullopt;
  }
  
  frame_id_t frame_id = it->second;
  size_t count = frames_[frame_id]->pin_count_.load();
  
  bpm_latch_->unlock();
  return count;
}

}  // namespace bustub
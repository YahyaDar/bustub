//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <memory>
#include "buffer/arc_replacer.h"
#include "common/macros.h"

namespace bustub {

ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                             std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  if (frame_ != nullptr) {
    frame_->rwlatch_.lock_shared();
  }
  is_valid_ = true;
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept {
  page_id_ = std::exchange(that.page_id_, INVALID_PAGE_ID);
  frame_ = std::exchange(that.frame_, nullptr);
  replacer_ = std::exchange(that.replacer_, nullptr);
  bpm_latch_ = std::exchange(that.bpm_latch_, nullptr);
  disk_scheduler_ = std::exchange(that.disk_scheduler_, nullptr);
  is_valid_ = std::exchange(that.is_valid_, false);
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & { 
  if (this != &that) {
    Drop();
    page_id_ = std::exchange(that.page_id_, INVALID_PAGE_ID);
    frame_ = std::exchange(that.frame_, nullptr);
    replacer_ = std::exchange(that.replacer_, nullptr);
    bpm_latch_ = std::exchange(that.bpm_latch_, nullptr);
    disk_scheduler_ = std::exchange(that.disk_scheduler_, nullptr);
    is_valid_ = std::exchange(that.is_valid_, false);
  }
  return *this; 
}

auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return page_id_;
}

auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->GetData();
}

auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->is_dirty_;
}

void ReadPageGuard::Flush() { 
  auto prom = disk_scheduler_->CreatePromise();
  auto fut = prom.get_future();
  bpm_latch_->lock();
  std::vector<DiskRequest> req;
  DiskRequest r{.is_write_ = true, .data_ = frame_->data_.data(), .page_id_ = page_id_, .callback_ = std::move(prom)};
  req.push_back(std::move(r));
  disk_scheduler_->Schedule(req);
  bpm_latch_->unlock();
  fut.get();
}

void ReadPageGuard::Drop() { 
  if (is_valid_ && frame_ != nullptr) {
    frame_->rwlatch_.unlock_shared(); 
    
    bpm_latch_->lock(); 
    frame_->pin_count_--; 
    if (frame_->pin_count_ == 0) {
      replacer_->SetEvictable(frame_->frame_id_, true); 
    }
    bpm_latch_->unlock(); 
    
    page_id_ = INVALID_PAGE_ID;
    frame_ = nullptr;
    replacer_ = nullptr;
    bpm_latch_ = nullptr;
    disk_scheduler_ = nullptr;
    is_valid_ = false;
  }
}

ReadPageGuard::~ReadPageGuard() { Drop(); }

/**********************************************************************************************************************/
/**********************************************************************************************************************/
/**********************************************************************************************************************/

WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<ArcReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch,
                               std::shared_ptr<DiskScheduler> disk_scheduler)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      disk_scheduler_(std::move(disk_scheduler)) {
  if (frame_ != nullptr) {
    frame_->rwlatch_.lock();
  }
  is_valid_ = true;
}

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept { 
  page_id_ = std::exchange(that.page_id_, INVALID_PAGE_ID); 
  frame_ = std::exchange(that.frame_, nullptr);
  replacer_ = std::exchange(that.replacer_, nullptr);
  bpm_latch_ = std::exchange(that.bpm_latch_, nullptr);
  disk_scheduler_ = std::exchange(that.disk_scheduler_, nullptr);
  is_valid_ = std::exchange(that.is_valid_, false);
}

auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & { 
  if (this != &that) {
    Drop(); 
    page_id_ = std::exchange(that.page_id_, INVALID_PAGE_ID);
    frame_ = std::exchange(that.frame_, nullptr);
    replacer_ = std::exchange(that.replacer_, nullptr);
    bpm_latch_ = std::exchange(that.bpm_latch_, nullptr);
    disk_scheduler_ = std::exchange(that.disk_scheduler_, nullptr);
    is_valid_ = std::exchange(that.is_valid_, false);
  }
  return *this; 
}

auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetDataMut();
}

auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

void WritePageGuard::Flush() {
  auto prom = disk_scheduler_->CreatePromise();
  auto fut = prom.get_future();
  bpm_latch_->lock();
  std::vector<DiskRequest> req;
  DiskRequest r{.is_write_ = true, .data_ = frame_->data_.data(), .page_id_ = page_id_, .callback_ = std::move(prom)};
  req.push_back(std::move(r));
  disk_scheduler_->Schedule(req);
  bpm_latch_->unlock();
  fut.get();
}

void WritePageGuard::Drop() { 
  if (is_valid_ && frame_ != nullptr) {
    frame_->is_dirty_ = true;
    frame_->rwlatch_.unlock(); 
    
    bpm_latch_->lock(); 
    frame_->pin_count_--; 
    if (frame_->pin_count_ == 0) {
      replacer_->SetEvictable(frame_->frame_id_, true); 
    }
    bpm_latch_->unlock(); 
    
    page_id_ = INVALID_PAGE_ID;
    frame_ = nullptr;
    replacer_ = nullptr;
    bpm_latch_ = nullptr;
    disk_scheduler_ = nullptr;
    is_valid_ = false;
  }
}

WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/**
 * Default constructor creates an end iterator.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

/**
 * Parameterized constructor.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, page_id_t page_id, int index)
    : bpm_(std::move(bpm)), page_id_(page_id), index_(index) {
  if (page_id_ != INVALID_PAGE_ID) {
    guard_ = bpm_->ReadPage(page_id_);
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return page_id_ == INVALID_PAGE_ID; }

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  auto *leaf = guard_->template As<LeafPage>();
  return {leaf->key_array_[index_], leaf->rid_array_[index_]};
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  auto *leaf = guard_->template As<LeafPage>();

  // Try to find the next non-tombstoned entry in the current page
  auto next_idx = leaf->Next(index_);
  if (next_idx.has_value()) {
    index_ = next_idx.value();
    return *this;
  }

  // Move to the next leaf page
  page_id_t next_page_id = leaf->GetNextPageId();
  guard_ = std::nullopt;  // Drop current guard

  if (next_page_id == INVALID_PAGE_ID) {
    // End of iteration
    page_id_ = INVALID_PAGE_ID;
    index_ = -1;
    return *this;
  }

  // Move to next page and find first non-tombstoned entry
  page_id_ = next_page_id;
  guard_ = bpm_->ReadPage(page_id_);
  leaf = guard_->template As<LeafPage>();

  // Find first non-tombstoned entry in the new page (starting from index -1)
  auto first_idx = leaf->Next(-1);
  if (first_idx.has_value()) {
    index_ = first_idx.value();
  } else {
    // Empty page (all tombstoned), keep moving
    page_id_ = INVALID_PAGE_ID;
    index_ = -1;
    guard_ = std::nullopt;
  }

  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub

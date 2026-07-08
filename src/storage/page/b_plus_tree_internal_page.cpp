//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  BUSTUB_ENSURE(max_size >= MIN_INTERNAL_PAGE_SIZE, "max_size must be >= MIN_INTERNAL_PAGE_SIZE");
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
}

/**
 * @brief Helper method to get the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return key_array_[index];
}

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  key_array_[index] = key;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return page_id_array_[index];
}

/**
 * @brief Uses std::upper_bound on the key range starting at key_array_ + 1
 * and ending at key_array_ + GetSize(). Intentionally skips the first key slot.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::UpperBound(const KeyType &key, const KeyComparator &comparator) const -> int {
  auto *start = key_array_ + 1;
  auto *end = key_array_ + GetSize();
  auto it = std::upper_bound(start, end, key, [&comparator](const KeyType &a, const KeyType &b) {
    return comparator(a, b) < 0;
  });
  return static_cast<int>(it - key_array_);
}

/**
 * @brief Returns UpperBound(key, comparator) - 1.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::GetTargetPageIndex(const KeyType &key, const KeyComparator &comparator) const
    -> int {
  return UpperBound(key, comparator) - 1;
}

/**
 * @brief Returns the child page id that should be followed for the given search key.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Search(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  int index = GetTargetPageIndex(key, comparator);
  return ValueAt(index);
}

/**
 * @brief Shifts both arrays right with std::memmove, stores the new key and page id, increments size.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertInto(int index, const KeyType &key, const ValueType &value) {
  int size = GetSize();
  // Shift keys right
  std::memmove(&key_array_[index + 1], &key_array_[index], (size - index) * sizeof(KeyType));
  // Shift page ids right
  std::memmove(&page_id_array_[index + 1], &page_id_array_[index], (size - index) * sizeof(ValueType));
  key_array_[index] = key;
  page_id_array_[index] = value;
  ChangeSizeBy(1);
}

/**
 * @brief Finds the insertion point using UpperBound and then delegates to InsertInto.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value,
                                            const KeyComparator &comparator) {
  int index = UpperBound(key, comparator);
  InsertInto(index, key, value);
}

/**
 * @brief Used when creating a new root internal page.
 * Sets key_array_[1] = key, page_id_array_[0] = value1, page_id_array_[1] = value2, size = 2.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(const KeyType &key, const ValueType &value1, const ValueType &value2) {
  key_array_[1] = key;
  page_id_array_[0] = value1;
  page_id_array_[1] = value2;
  SetSize(2);
}

/**
 * @brief Internal-node split routine.
 *
 * Computes mid_index = total_size / 2.
 * If the incoming key sorts to the right of the midpoint key, increment the midpoint
 * and mark that the insert should happen in the new right page.
 * Copy the upper half of keys and child pointers into the other page.
 * Insert the new pair into either the original page or the new page depending on the split position.
 * Return the first key of the new right page as the separator key.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::SplitAndInsert(BPlusTreeInternalPage *other, const KeyType &key,
                                                    const ValueType &value, const KeyComparator &comparator)
    -> KeyType {
  int total_size = GetSize();
  
  std::vector<KeyType> tmp_keys(total_size + 1);
  std::vector<ValueType> tmp_values(total_size + 1);
  
  int insert_idx = UpperBound(key, comparator);
  
  // Copy to tmp
  for (int i = 0; i < insert_idx; i++) {
    tmp_keys[i] = key_array_[i];
    tmp_values[i] = page_id_array_[i];
  }
  tmp_keys[insert_idx] = key;
  tmp_values[insert_idx] = value;
  for (int i = insert_idx; i < total_size; i++) {
    tmp_keys[i + 1] = key_array_[i];
    tmp_values[i + 1] = page_id_array_[i];
  }
  
  int mid_index = (total_size + 1) / 2;
  
  // Copy back to this
  for (int i = 0; i < mid_index; i++) {
    key_array_[i] = tmp_keys[i];
    page_id_array_[i] = tmp_values[i];
  }
  SetSize(mid_index);
  
  // Copy to other
  int right_count = total_size + 1 - mid_index;
  for (int i = 0; i < right_count; i++) {
    other->key_array_[i] = tmp_keys[mid_index + i];
    other->page_id_array_[i] = tmp_values[mid_index + i];
  }
  other->SetSize(right_count);
  
  return other->key_array_[0];
}

/**
 * @brief Computes:
 * - the current child pointer for a given search key
 * - a neighboring sibling pointer to the left if available, otherwise to the right
 * - the sibling's index and whether the sibling is left or right
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SearchCurrentAndSibling(const KeyType &key, const KeyComparator &comparator,
                                                             page_id_t *current, page_id_t *sibling,
                                                             int *sibling_index, bool *is_left_sibling) {
  int index = GetTargetPageIndex(key, comparator);
  *current = ValueAt(index);

  // Prefer left sibling if available
  if (index > 0) {
    *sibling = ValueAt(index - 1);
    *sibling_index = index;  // The separator key index between sibling and current
    *is_left_sibling = true;
  } else {
    *sibling = ValueAt(index + 1);
    *sibling_index = index + 1;  // The separator key index between current and sibling
    *is_left_sibling = false;
  }
}

/**
 * @brief Removes one slot by shifting the tail of the arrays left and decrementing size.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(size_t index) {
  int size = GetSize();
  int count = size - static_cast<int>(index) - 1;
  if (count > 0) {
    std::memmove(&key_array_[index], &key_array_[index + 1], count * sizeof(KeyType));
    std::memmove(&page_id_array_[index], &page_id_array_[index + 1], count * sizeof(ValueType));
  }
  ChangeSizeBy(-1);
}

/**
 * @brief Removes the rightmost entry from the current page and inserts it at the front of the right page.
 * Returns the separator key that should be propagated upward.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::LendToRight(BPlusTreeInternalPage *right, const KeyType &parent_key) -> KeyType {
  int size = GetSize();

  // The rightmost entry's key/value
  KeyType last_key = key_array_[size - 1];
  ValueType last_value = page_id_array_[size - 1];

  // Insert at front of right page: use the parent_key as the key for the entry
  right->InsertInto(0, parent_key, last_value);

  // Remove from current page
  ChangeSizeBy(-1);

  // The separator key to propagate upward is the last key we removed
  return last_key;
}

/**
 * @brief Removes the leftmost entry from the current page and appends it to the left page.
 * Returns the separator key that should be propagated upward.
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::LendToLeft(BPlusTreeInternalPage *left, const KeyType &parent_key) -> KeyType {
  // The leftmost entry
  KeyType first_key = key_array_[1];  // key at index 1 is the first valid key
  ValueType first_value = page_id_array_[0];

  // Append to left page: use parent_key as the separator
  int left_size = left->GetSize();
  left->key_array_[left_size] = parent_key;
  left->page_id_array_[left_size] = first_value;
  left->ChangeSizeBy(1);

  // Remove from current page
  Remove(0);

  // The separator key to propagate upward is the first valid key of what was removed
  return first_key;
}

/**
 * @brief Appends the right page's keys and child pointers onto the end of the current page
 * and clears the right page's size.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Merge(BPlusTreeInternalPage *right, const KeyType &parent_key) {
  int size = GetSize();
  int right_size = right->GetSize();

  // The parent key becomes the key for the first child of the right page
  key_array_[size] = parent_key;
  page_id_array_[size] = right->page_id_array_[0];

  // Copy the rest of right's entries
  for (int i = 1; i < right_size; i++) {
    key_array_[size + i] = right->key_array_[i];
    page_id_array_[size + i] = right->page_id_array_[i];
  }

  ChangeSizeBy(right_size);
  right->SetSize(0);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub

//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {



FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto header = guard.As<BPlusTreeHeaderPage>();
  return header->root_page_id_ == INVALID_PAGE_ID;
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/

/**
 * @brief Recursive lookup - descends from given page to find the key.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Lookup(Context &ctx, const KeyType &key, page_id_t page_id) -> std::optional<ValueType> {
  ReadPageGuard guard = bpm_->ReadPage(page_id);
  auto *page = guard.As<BPlusTreePage>();

  if (page->IsLeafPage()) {
    auto *leaf = guard.As<LeafPage>();
    return leaf->Lookup(key, comparator_);
  }

  // Internal page
  auto *internal = guard.As<InternalPage>();
  page_id_t child_id = internal->Search(key, comparator_);

  // Drop current guard before recursing (no need to hold parent locks for reads)
  guard.Drop();

  return Lookup(ctx, key, child_id);
}

/**
 * @brief Return the only value that associated with input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  Context ctx;

  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  auto header = header_guard.As<BPlusTreeHeaderPage>();

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }

  ctx.root_page_id_ = header->root_page_id_;
  page_id_t root_id = header->root_page_id_;
  header_guard.Drop();

  auto value = Lookup(ctx, key, root_id);
  if (value.has_value()) {
    result->push_back(value.value());
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/

/**
 * @brief Insert into a leaf page. Handles splits if necessary.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoLeafPage(LeafPage *leaf, const KeyType &key, const ValueType &value,
                                        BPlusTreeInsertRet<KeyType> &ret, bool optimistic) {
  // Check for duplicate
  if (leaf->Exist(key, comparator_)) {
    ret.status_ = BPlusTreeOpStatus::Duplicate;
    return;
  }

  // If not full, insert directly
  if (!leaf->IsFull()) {
    leaf->Insert(key, value, comparator_);
    ret.status_ = BPlusTreeOpStatus::Success;
    return;
  }

  // Page is full
  if (optimistic) {
    ret.status_ = BPlusTreeOpStatus::OptimisticLockFailed;
    return;
  }

  // Pessimistic mode: split the page
  page_id_t new_page_id = bpm_->NewPage();
  WritePageGuard new_guard = bpm_->WritePage(new_page_id);
  auto *new_leaf = new_guard.AsMut<LeafPage>();
  new_leaf->Init(leaf_max_size_);

  // Split
  leaf->Split(new_leaf);
  leaf->SetNextPageId(new_page_id);

  // Insert into the correct half
  if (comparator_(key, new_leaf->KeyAt(0)) >= 0) {
    new_leaf->Insert(key, value, comparator_);
  } else {
    leaf->Insert(key, value, comparator_);
  }

  // Report split
  ret.status_ = BPlusTreeOpStatus::Success;
  ret.split_ = true;
  ret.new_page_id_ = new_page_id;
  ret.split_key_ = new_leaf->KeyAt(0);
}

/**
 * @brief Insert into an internal page. Handles splits if necessary.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoInternalPage(InternalPage *internal, const KeyType &key, page_id_t child_id,
                                            BPlusTreeInsertRet<KeyType> &ret) {
  if (!internal->IsFull()) {
    internal->Insert(key, child_id, comparator_);
    ret.split_ = false;
    return;
  }

  // Internal page is full - split
  page_id_t new_page_id = bpm_->NewPage();
  WritePageGuard new_guard = bpm_->WritePage(new_page_id);
  auto *new_internal = new_guard.AsMut<InternalPage>();
  new_internal->Init(internal_max_size_);

  KeyType separator = internal->SplitAndInsert(new_internal, key, child_id, comparator_);

  ret.split_ = true;
  ret.new_page_id_ = new_page_id;
  ret.split_key_ = separator;
}

/**
 * @brief Create a new root when a split reaches the root.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitRootPage(BPlusTreeHeaderPage *header, page_id_t old_root_id,
                                   const KeyType &split_key, page_id_t new_right_id) {
  page_id_t new_root_id = bpm_->NewPage();
  WritePageGuard new_root_guard = bpm_->WritePage(new_root_id);
  auto *new_root = new_root_guard.AsMut<InternalPage>();
  new_root->Init(internal_max_size_);
  new_root->Init(split_key, old_root_id, new_right_id);

  header->root_page_id_ = new_root_id;
}

/**
 * @brief Recursive insert.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertRecursive(Context &ctx, const KeyType &key, const ValueType &value,
                                     page_id_t page_id, BPlusTreeInsertRet<KeyType> &ret, bool optimistic) {
  if (page_id == INVALID_PAGE_ID) {
    // Insert at root level (create new root leaf)
    if (optimistic) {
      ret.status_ = BPlusTreeOpStatus::OptimisticLockFailed;
      return;
    }

    // Create a new leaf as the root
    page_id_t new_page_id = bpm_->NewPage();
    WritePageGuard new_guard = bpm_->WritePage(new_page_id);
    auto *leaf = new_guard.AsMut<LeafPage>();
    leaf->Init(leaf_max_size_);
    leaf->Insert(key, value, comparator_);

    // Update header
    auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    header->root_page_id_ = new_page_id;
    ctx.root_page_id_ = new_page_id;

    ret.status_ = BPlusTreeOpStatus::Success;
    return;
  }

  if (optimistic) {
    // Optimistic: use read guard, upgrade only at leaf
    ReadPageGuard read_guard = bpm_->ReadPage(page_id);
    auto *page = read_guard.As<BPlusTreePage>();

    if (page->IsLeafPage()) {
      read_guard.Drop();
      // Upgrade to write guard for the leaf
      WritePageGuard write_guard = bpm_->WritePage(page_id);
      auto *leaf = write_guard.AsMut<LeafPage>();
      InsertIntoLeafPage(leaf, key, value, ret, true);
      return;
    }

    // Internal page
    auto *internal = read_guard.As<InternalPage>();
    page_id_t child_id = internal->Search(key, comparator_);
    read_guard.Drop();

    InsertRecursive(ctx, key, value, child_id, ret, true);
    return;
  }

  // Pessimistic mode
  WritePageGuard write_guard = bpm_->WritePage(page_id);
  auto *page = write_guard.AsMut<BPlusTreePage>();

  if (page->IsLeafPage()) {
    auto *leaf = write_guard.AsMut<LeafPage>();
    InsertIntoLeafPage(leaf, key, value, ret, false);
    return;
  }

  // Internal page
  auto *internal = write_guard.AsMut<InternalPage>();

  // Check if we can release ancestors (safe node)
  if (internal->CanReleaseAncestor(true)) {
    ctx.header_page_ = std::nullopt;
    while (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_front();
    }
  }

  page_id_t child_id = internal->Search(key, comparator_);

  // Store current page in write set
  ctx.write_set_.push_back(std::move(write_guard));

  InsertRecursive(ctx, key, value, child_id, ret, false);

  // Handle split bubbling up
  if (ret.status_ == BPlusTreeOpStatus::Success && ret.split_) {
    auto &parent_guard = ctx.write_set_.back();
    auto *parent_internal = parent_guard.AsMut<InternalPage>();

    BPlusTreeInsertRet<KeyType> parent_ret;
    parent_ret.status_ = BPlusTreeOpStatus::Success;
    InsertIntoInternalPage(parent_internal, ret.split_key_, ret.new_page_id_, parent_ret);

    ctx.write_set_.pop_back();

    // Propagate split info up
    ret.split_ = parent_ret.split_;
    ret.new_page_id_ = parent_ret.new_page_id_;
    ret.split_key_ = parent_ret.split_key_;
  } else {
    // No split to handle, just pop our guard
    if (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_back();
    }
  }
}

/**
 * @brief Public insert.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  Context ctx;
  BPlusTreeInsertRet<KeyType> ret;

  // Try optimistic first
  {
    ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
    auto *header = header_guard.As<BPlusTreeHeaderPage>();

    if (header->root_page_id_ == INVALID_PAGE_ID) {
      // Empty tree - need pessimistic mode
      header_guard.Drop();
      goto pessimistic;
    }

    ctx.root_page_id_ = header->root_page_id_;
    page_id_t root_id = header->root_page_id_;
    header_guard.Drop();

    InsertRecursive(ctx, key, value, root_id, ret, true);

    if (ret.status_ == BPlusTreeOpStatus::Success) {
      return true;
    }
    if (ret.status_ == BPlusTreeOpStatus::Duplicate) {
      return false;
    }
  }

pessimistic:
  // Pessimistic mode
  ctx = Context();
  ret = BPlusTreeInsertRet<KeyType>();

  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  page_id_t root_id = header->root_page_id_;

  InsertRecursive(ctx, key, value, root_id, ret, false);

  if (ret.status_ == BPlusTreeOpStatus::Success && ret.split_) {
    // Root was split - create a new root
    SplitRootPage(header, ctx.root_page_id_, ret.split_key_, ret.new_page_id_);
  }

  return ret.status_ == BPlusTreeOpStatus::Success;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/


/**
 * @brief Delete from a leaf page.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::DeleteFromLeafPage(LeafPage *leaf, const KeyType &key,
                                        BPlusTreeDeleteRet<KeyType> &ret, bool optimistic,
                                        page_id_t page_id) {
  auto idx = leaf->LookupIndex(key, comparator_);
  if (!idx.has_value()) {
    ret.status_ = BPlusTreeOpStatus::NotFound;
    return;
  }

  if (optimistic) {
    bool can_safe = leaf->CanSafeRemove();
    std::cout << "Optimistic Delete " << key.GetAsInteger() << ": CanSafeRemove=" << can_safe << " size=" << leaf->GetSize() << " tombs=" << leaf->GetTombstones().size() << std::endl;
    if (can_safe) {
      leaf->SoftRemove(key, comparator_);
      ret.status_ = BPlusTreeOpStatus::Success;
      return;
    }
    ret.status_ = BPlusTreeOpStatus::OptimisticLockFailed;
    return;
  }
  
  std::cout << "Pessimistic Delete " << key.GetAsInteger() << std::endl;

  // Pessimistic mode
  leaf->Remove(idx.value());
  ret.status_ = BPlusTreeOpStatus::Success;

  // Check for underflow
  if (!leaf->Underflow()) {
    return;
  }

  // If this is the root leaf
  if (page_id == ctx_root_page_id_placeholder_) {
    // Will be handled at the root level by caller
    if (leaf->GetSize() == 0 || (leaf->GetSize() - static_cast<int>(leaf->num_tombstones_) == 0)) {
      ret.deleted_page_id_ = page_id;
    }
  }
}

/**
 * @brief Delete from an internal page.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::DeleteFromInternalPage(InternalPage *internal, size_t index,
                                            BPlusTreeDeleteRet<KeyType> &ret, page_id_t page_id) {
  internal->Remove(index);
  ret.status_ = BPlusTreeOpStatus::Success;

  if (!internal->Underflow()) {
    return;
  }

  // If this is the root
  if (internal->GetSize() == 1) {
    // Collapse root to its only child
    ret.deleted_page_id_ = page_id;
  }
}

/**
 * @brief Recursive remove.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveRecursive(Context &ctx, const KeyType &key, page_id_t page_id,
                                     BPlusTreeDeleteRet<KeyType> &ret, bool optimistic) {
  if (optimistic) {
    ReadPageGuard read_guard = bpm_->ReadPage(page_id);
    auto *page = read_guard.As<BPlusTreePage>();

    if (page->IsLeafPage()) {
      read_guard.Drop();
      WritePageGuard write_guard = bpm_->WritePage(page_id);
      auto *leaf = write_guard.AsMut<LeafPage>();
      auto idx = leaf->LookupIndex(key, comparator_);
      if (!idx.has_value()) {
        ret.status_ = BPlusTreeOpStatus::NotFound;
        return;
      }
      bool can_safe = leaf->CanSafeRemove();
      std::cout << "Optimistic Delete " << key.GetAsInteger() << ": CanSafeRemove=" << can_safe << " size=" << leaf->GetSize() << " tombs=" << leaf->GetTombstones().size() << std::endl;
      if (can_safe) {
        leaf->SoftRemove(key, comparator_);
        ret.status_ = BPlusTreeOpStatus::Success;
        return;
      }
      ret.status_ = BPlusTreeOpStatus::OptimisticLockFailed;
      return;
    }

    // Internal page
    auto *internal = read_guard.As<InternalPage>();
    page_id_t child_id = internal->Search(key, comparator_);
    read_guard.Drop();

    RemoveRecursive(ctx, key, child_id, ret, true);
    return;
  }

  // Pessimistic mode
  WritePageGuard write_guard = bpm_->WritePage(page_id);
  auto *page = write_guard.AsMut<BPlusTreePage>();

  if (page->IsLeafPage()) {
    auto *leaf = write_guard.AsMut<LeafPage>();

    auto idx = leaf->LookupIndex(key, comparator_);
    if (!idx.has_value()) {
      ret.status_ = BPlusTreeOpStatus::NotFound;
      return;
    }

    std::cout << "Pessimistic Delete " << key.GetAsInteger() << std::endl;
    leaf->Remove(idx.value());
    ret.status_ = BPlusTreeOpStatus::Success;

    if (!leaf->Underflow()) {
      return;
    }

    // Check if this is the root
    if (ctx.IsRootPage(page_id)) {
      if (leaf->GetSize() == 0) {
        ret.deleted_page_id_ = page_id;
      }
      return;
    }

    // Need to rebalance with sibling
    // The parent is the last entry in write_set_
    if (ctx.write_set_.empty()) {
      return;
    }

    auto &parent_guard = ctx.write_set_.back();
    auto *parent = parent_guard.AsMut<InternalPage>();


    // We need to find our actual page_id_t index in the parent
    // Actually let's use a different approach: find the child index
    int child_idx = -1;
    for (int i = 0; i < parent->GetSize(); i++) {
      if (parent->ValueAt(i) == page_id) {
        child_idx = i;
        break;
      }
    }

    int sib_idx;
    bool is_left_sibling;
    int sibling_index;
    page_id_t sibling_id;
    if (child_idx > 0) {
      sib_idx = child_idx - 1;
      is_left_sibling = true;
      sibling_index = child_idx;  // Key index between left sib and current
    } else {
      sib_idx = child_idx + 1;
      is_left_sibling = false;
      sibling_index = child_idx + 1;  // Key index between current and right sib
    }
    sibling_id = parent->ValueAt(sib_idx);

    WritePageGuard sibling_guard = bpm_->WritePage(sibling_id);
    auto *sibling_leaf = sibling_guard.AsMut<LeafPage>();



    int logical_size = sibling_leaf->GetSize() - static_cast<int>(sibling_leaf->GetTombstones().size());
    if (logical_size > sibling_leaf->GetMinSize()) {
      // Redistribute
      KeyType new_separator;
      if (is_left_sibling) {
        new_separator = sibling_leaf->LendToRight(leaf);
      } else {
        new_separator = sibling_leaf->LendToLeft(leaf);
      }
      parent->SetKeyAt(sibling_index, new_separator);
    } else {
      // Merge
      if (is_left_sibling) {
        sibling_leaf->Merge(leaf);
        ret.deleted_page_id_ = page_id;
      } else {
        leaf->Merge(sibling_leaf);
        ret.deleted_page_id_ = sibling_id;
      }
      if (ctx.IsRootPage(parent_guard.GetPageId()) && parent->GetSize() == 1) {
        // Root collapse
        ret.deleted_page_id_ = parent_guard.GetPageId();
      } else if (parent->Underflow() && !ctx.IsRootPage(parent_guard.GetPageId())) {
        // Parent needs rebalancing - this will be handled by the caller
        // For now, we just signal through deleted_page_id
      }
    }
    return;
  }

  // Internal page
  auto *internal = write_guard.AsMut<InternalPage>();

  // Check if we can release ancestors
  if (internal->CanReleaseAncestor(false)) {
    ctx.header_page_ = std::nullopt;
    while (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_front();
    }
  }

  page_id_t child_id = internal->Search(key, comparator_);


  ctx.write_set_.push_back(std::move(write_guard));

  RemoveRecursive(ctx, key, child_id, ret, false);

  if (ret.status_ != BPlusTreeOpStatus::Success) {
    if (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_back();
    }
    return;
  }

  if (ret.deleted_page_id_ == INVALID_PAGE_ID) {
    if (!ctx.write_set_.empty()) {
      ctx.write_set_.pop_back();
    }
    return;
  }

  // A child was deleted/merged - need to handle at this level
  auto &cur_guard = ctx.write_set_.back();
  auto *cur_internal = cur_guard.AsMut<InternalPage>();
  page_id_t cur_page_id = cur_guard.GetPageId();

  // The deleted page might have triggered a root collapse down below
  // Find the index of the deleted child
  int del_idx = -1;
  for (int i = 0; i < cur_internal->GetSize(); i++) {
    if (cur_internal->ValueAt(i) == ret.deleted_page_id_) {
      del_idx = i;
      break;
    }
  }

  if (del_idx == -1) {
    // The deleted page is ourselves (root collapse from child level)
    // Already handled
    ctx.write_set_.pop_back();
    return;
  }

  // Remove the entry for the deleted child
  cur_internal->Remove(static_cast<size_t>(del_idx));
  bpm_->DeletePage(ret.deleted_page_id_);
  ret.deleted_page_id_ = INVALID_PAGE_ID;

  if (!cur_internal->Underflow()) {
    ctx.write_set_.pop_back();
    return;
  }

  // Root internal page with only 1 child - collapse
  if (ctx.IsRootPage(cur_page_id) && cur_internal->GetSize() == 1) {
    ret.deleted_page_id_ = cur_page_id;
    ctx.write_set_.pop_back();
    return;
  }

  if (ctx.IsRootPage(cur_page_id)) {
    ctx.write_set_.pop_back();
    return;
  }

  // Need to rebalance with sibling internal page
  // Parent is the entry before us in write_set_
  if (ctx.write_set_.size() < 2) {
    ctx.write_set_.pop_back();
    return;
  }

  auto cur_guard_moved = std::move(ctx.write_set_.back());
  ctx.write_set_.pop_back();
  auto &parent_guard = ctx.write_set_.back();
  auto *parent = parent_guard.AsMut<InternalPage>();

  // Find our index in the parent
  int my_idx = -1;
  for (int i = 0; i < parent->GetSize(); i++) {
    if (parent->ValueAt(i) == cur_page_id) {
      my_idx = i;
      break;
    }
  }

  int sib_idx;
  bool is_left_sibling;
  int separator_idx;
  if (my_idx > 0) {
    sib_idx = my_idx - 1;
    is_left_sibling = true;
    separator_idx = my_idx;
  } else {
    sib_idx = my_idx + 1;
    is_left_sibling = false;
    separator_idx = my_idx + 1;
  }

  page_id_t sibling_id = parent->ValueAt(sib_idx);
  WritePageGuard sibling_guard = bpm_->WritePage(sibling_id);
  auto *sibling_internal = sibling_guard.AsMut<InternalPage>();

  KeyType parent_key = parent->KeyAt(separator_idx);

  if (sibling_internal->CanLendAKey()) {
    // Redistribute
    KeyType new_separator;
    if (is_left_sibling) {
      new_separator = sibling_internal->LendToRight(cur_internal, parent_key);
    } else {
      new_separator = sibling_internal->LendToLeft(cur_guard_moved.AsMut<InternalPage>(), parent_key);
    }
    parent->SetKeyAt(separator_idx, new_separator);
  } else {
    // Merge
    if (is_left_sibling) {
      sibling_internal->Merge(cur_internal, parent_key);
      ret.deleted_page_id_ = cur_page_id;
    } else {
      cur_internal->Merge(sibling_internal, parent_key);
      ret.deleted_page_id_ = sibling_id;
    }
    if (ctx.IsRootPage(parent_guard.GetPageId()) && parent->GetSize() == 1) {
      // Need to report root collapse upward
      page_id_t to_delete = ret.deleted_page_id_;
      bpm_->DeletePage(to_delete);
      ret.deleted_page_id_ = parent_guard.GetPageId();
    }
  }
}

/**
 * @brief Public remove.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  Context ctx;
  BPlusTreeDeleteRet<KeyType> ret;

  // Try optimistic first
  {
    ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
    auto *header = header_guard.As<BPlusTreeHeaderPage>();

    if (header->root_page_id_ == INVALID_PAGE_ID) {
      return;  // Empty tree
    }

    ctx.root_page_id_ = header->root_page_id_;
    page_id_t root_id = header->root_page_id_;
    header_guard.Drop();

    RemoveRecursive(ctx, key, root_id, ret, true);

    if (ret.status_ == BPlusTreeOpStatus::Success || ret.status_ == BPlusTreeOpStatus::NotFound) {
      return;
    }
  }

  // Pessimistic mode
  ctx = Context();
  ret = BPlusTreeDeleteRet<KeyType>();

  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto *header = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = header->root_page_id_;

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    return;
  }

  page_id_t root_id = header->root_page_id_;

  RemoveRecursive(ctx, key, root_id, ret, false);

  if (ret.status_ == BPlusTreeOpStatus::Success && ret.deleted_page_id_ != INVALID_PAGE_ID) {
    if (ret.deleted_page_id_ == root_id) {
      // Root was deleted
      // Read the root to see if it has a child to promote
      WritePageGuard root_guard = bpm_->WritePage(root_id);
      auto *root_page = root_guard.AsMut<BPlusTreePage>();

      if (root_page->IsLeafPage()) {
        // Empty leaf root - tree is now empty
        header->root_page_id_ = INVALID_PAGE_ID;
      } else {
        // Internal root with one child - promote the child
        auto *root_internal = root_guard.AsMut<InternalPage>();
        header->root_page_id_ = root_internal->ValueAt(0);
      }
      root_guard.Drop();
      bpm_->DeletePage(root_id);
    } else {
      bpm_->DeletePage(ret.deleted_page_id_);
    }
  }
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/

/**
 * @brief Get iterator to the leftmost leaf, positioned at the first live key.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  return GetIterator(std::nullopt);
}

/**
 * @brief Get iterator positioned at the first key >= input key.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  return GetIterator(std::optional<KeyType>(key));
}

/**
 * @brief End sentinel.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/**
 * @brief Traversal to find the leaf and create an iterator.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetIterator(const std::optional<KeyType> &&key) -> INDEXITERATOR_TYPE {
  ReadPageGuard header_guard = bpm_->ReadPage(header_page_id_);
  auto *header = header_guard.As<BPlusTreeHeaderPage>();

  if (header->root_page_id_ == INVALID_PAGE_ID) {
    return End();
  }

  page_id_t page_id = header->root_page_id_;
  header_guard.Drop();

  // Descend through internal pages
  while (true) {
    ReadPageGuard guard = bpm_->ReadPage(page_id);
    auto *page = guard.As<BPlusTreePage>();

    if (page->IsLeafPage()) {
      auto *leaf = guard.As<LeafPage>();

      if (key.has_value()) {
        auto idx = leaf->GetLowerBoundIndex(key.value(), comparator_);
        if (idx.has_value()) {
          guard.Drop();
          return INDEXITERATOR_TYPE(bpm_, page_id, idx.value());
        }
        // Key not in this leaf, try next
        page_id_t next = leaf->GetNextPageId();
        guard.Drop();

        while (next != INVALID_PAGE_ID) {
          ReadPageGuard next_guard = bpm_->ReadPage(next);
          auto *next_leaf = next_guard.As<LeafPage>();
          auto next_idx = next_leaf->GetLowerBoundIndex(key.value(), comparator_);
          if (next_idx.has_value()) {
            page_id_t found_page = next;
            next_guard.Drop();
            return INDEXITERATOR_TYPE(bpm_, found_page, next_idx.value());
          }
          next = next_leaf->GetNextPageId();
          next_guard.Drop();
        }

        return End();
      }

      // Begin() without key: find first live entry
      auto first = leaf->Next(-1);
      if (first.has_value()) {
        guard.Drop();
        return INDEXITERATOR_TYPE(bpm_, page_id, first.value());
      }

      // All tombstoned in this page, try next
      page_id_t next = leaf->GetNextPageId();
      guard.Drop();
      if (next != INVALID_PAGE_ID) {
        ReadPageGuard next_guard = bpm_->ReadPage(next);
        auto *next_leaf = next_guard.As<LeafPage>();
        auto next_first = next_leaf->Next(-1);
        if (next_first.has_value()) {
          next_guard.Drop();
          return INDEXITERATOR_TYPE(bpm_, next, next_first.value());
        }
        next_guard.Drop();
      }
      return End();
    }

    // Internal page: descend
    auto *internal = guard.As<InternalPage>();
    if (key.has_value()) {
      page_id = internal->Search(key.value(), comparator_);
    } else {
      page_id = internal->ValueAt(0);  // Leftmost child
    }
    guard.Drop();
  }
}

/**
 * @brief Returns the root page id.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto *header = guard.AsMut<BPlusTreeHeaderPage>();
  return header->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub

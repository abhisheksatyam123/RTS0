#ifndef RTSO_ID_INDEX_H
#define RTSO_ID_INDEX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstdlib>
#include <vector>

namespace rtso {

// Entity id -> array index, for the ids alloc_id() hands out (dense, from 1 up): the subset of
// std::unordered_map<uint32_t, size_t> the simulation uses (find/end/at/[]/count/erase/clear),
// looked up by the id itself in pages of 4096 entries instead of hashed. An entry's address
// never moves, as an unordered_map node's doesn't. It can't be iterated: nothing walks these
// maps, and their order must never decide anything.
class IdIndex {
public:
	struct Entry {
		uint32_t first = kEmpty;
		size_t second = 0;
	};
	using iterator = Entry *;
	using const_iterator = const Entry *;

	IdIndex() = default;
	IdIndex(IdIndex &&) = default;
	IdIndex &operator=(IdIndex &&) = default;
	IdIndex(const IdIndex &o) { *this = o; }
	IdIndex &operator=(const IdIndex &o) {
		if (this == &o) return *this;
		pages_.clear();
		pages_.resize(o.pages_.size());
		for (size_t p = 0; p < o.pages_.size(); ++p)
			if (o.pages_[p]) pages_[p] = std::make_unique<Page>(*o.pages_[p]);
		size_ = o.size_;
		ids_ = o.ids_;
		return *this;
	}

	iterator end() { return nullptr; }
	const_iterator end() const { return nullptr; }

	iterator find(uint32_t id) { return const_cast<Entry *>(static_cast<const IdIndex *>(this)->find(id)); }
	const_iterator find(uint32_t id) const {
		const size_t p = id >> kPageBits;
		if (p >= pages_.size() || !pages_[p]) return nullptr;
		const Entry &e = (*pages_[p])[id & kPageMask];
		return e.first == id ? &e : nullptr;
	}
	size_t count(uint32_t id) const { return find(id) ? 1 : 0; }
	size_t &at(uint32_t id) { return const_cast<size_t &>(static_cast<const IdIndex *>(this)->at(id)); }
	const size_t &at(uint32_t id) const {
		const Entry *e = find(id);
		if (!e) std::abort(); // a missing id: unordered_map::at would throw, and the extension has no exceptions
		return e->second;
	}
	// Inserts the id with index 0 when it is missing, as unordered_map's operator[] does.
	size_t &operator[](uint32_t id) {
		if (id == kEmpty) std::abort();
		const size_t p = id >> kPageBits;
		if (p >= pages_.size()) pages_.resize(p + 1);
		if (!pages_[p]) pages_[p] = std::make_unique<Page>();
		Entry &e = (*pages_[p])[id & kPageMask];
		if (e.first != id) {
			e.first = id;
			e.second = 0;
			++size_;
			ids_.push_back(id);
		}
		return e.second;
	}
	size_t erase(uint32_t id) {
		Entry *e = find(id);
		if (!e) return 0;
		*e = Entry{};
		--size_;
		return 1;
	}
	// Empties the entries ever filled since the last clear, not every page.
	void clear() {
		for (uint32_t id : ids_) (*pages_[id >> kPageBits])[id & kPageMask] = Entry{};
		ids_.clear();
		size_ = 0;
	}
	size_t size() const { return size_; }
	bool empty() const { return size_ == 0; }

private:
	static constexpr uint32_t kEmpty = 0xFFFFFFFFu;
	static constexpr unsigned kPageBits = 12;
	static constexpr uint32_t kPageMask = (1u << kPageBits) - 1;
	using Page = std::array<Entry, 1u << kPageBits>;
	std::vector<std::unique_ptr<Page>> pages_;
	std::vector<uint32_t> ids_; // every id inserted since the last clear (erased ones too)
	size_t size_ = 0;
};

} // namespace rtso

#endif

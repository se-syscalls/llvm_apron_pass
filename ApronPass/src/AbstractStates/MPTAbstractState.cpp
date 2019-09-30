#include <AbstractStates/MPTAbstractState.h>

#include <cassert>
#include <algorithm>

// MPTItemAbstractState

MPTItemAbstractState::MPTItemAbstractState(
		const std::set<std::string> & buffers, bool isWritable) :
				m_buffers(buffers), m_isWritable(isWritable) {}

MPTItemAbstractState::MPTItemAbstractState() : m_isWritable(true) {}

const std::set<std::string> & MPTItemAbstractState::getBuffers() const {
	return m_buffers;
}

void MPTItemAbstractState::insert(std::string buffer) {
	if (m_isWritable) {
		m_buffers.insert(buffer);
	}
}

void MPTItemAbstractState::erase(std::string buffer) {
	if (m_isWritable) {
		m_buffers.erase(buffer);
	}
}

void MPTItemAbstractState::clear() {
	if (m_isWritable) {
		m_buffers.clear();
	}
}

std::set<std::string>::const_iterator MPTItemAbstractState::begin() const {
	return m_buffers.begin();
}

std::set<std::string>::const_iterator MPTItemAbstractState::end() const {
	return m_buffers.end();
}

bool MPTItemAbstractState::join(const MPTItemAbstractState & other) {
	assert(isWritable() || (m_buffers == other.m_buffers));
	bool isChanged = false;
	for (const std::string & userPtr : other.m_buffers) {
		auto inserted = m_buffers.insert(userPtr);
		isChanged = isChanged || inserted.second;
	}
	return isChanged;
}

bool MPTItemAbstractState::meet(const MPTItemAbstractState & other) {
	assert(isWritable() || (m_buffers == other.m_buffers));
	bool isChanged = false;
	for (std::set<std::string>::iterator it = m_buffers.begin(), ie = m_buffers.end();
			it != ie; it++) {
		if (other.contains(*it)) {
			continue;
		}
		it = m_buffers.erase(it);
		isChanged = true;
	}
	return isChanged;
}

void MPTItemAbstractState::lock() {
	m_isWritable = false;
}

bool MPTItemAbstractState::isProvablyNull() const {
	return (m_buffers.size() == 1) && (m_buffers.count("null") == 1);
}

bool MPTItemAbstractState::isProvablyKernel() const {
	return (m_buffers.size() == 1) && (m_buffers.count("kernel") == 1);
}

bool MPTItemAbstractState::empty() const {
	return m_buffers.empty();
}

unsigned MPTItemAbstractState::count() const {
	return m_buffers.size();
}

bool MPTItemAbstractState::isWritable() const {
	return m_isWritable;
}

bool MPTItemAbstractState::contains(const std::string & name) const {
	return (m_buffers.count(name) == 1);
}

bool MPTItemAbstractState::operator==(const MPTItemAbstractState & other) const {
	return (m_buffers == other.m_buffers);
}

bool MPTItemAbstractState::operator!=(const MPTItemAbstractState & other) const {
	return !(*this == other);
}

MPTItemAbstractState & MPTItemAbstractState::operator=(const MPTItemAbstractState & other) {
	m_buffers = other.m_buffers;
	return *this;
}

void MPTItemAbstractState::updateToIntersection(MPTItemAbstractState & left, MPTItemAbstractState & right) {
	std::set<std::string> intersection;
	std::set_intersection(left.begin(), left.end(), right.begin(), right.end(),
			std::inserter(intersection, intersection.end()));
	if (left.m_isWritable) {
		left.clear();
		left.m_buffers.insert(intersection.begin(), intersection.end());
	}
	if (right.m_isWritable) {
		right.clear();
		right.m_buffers.insert(intersection.begin(), intersection.end());
	}
}
// MPTAbstractState

MPTAbstractState::MPTAbstractState() {
	m_mayPointsTo.insert(std::make_pair("null",
			MPTItemAbstractState({"null"}, false)));
}

MPTAbstractState::MPTAbstractState(std::vector<std::string> buffers) {
	for (const std::string & buffer : buffers) {
		std::set<std::string> identBuffers = {buffer, "null"};
		m_mayPointsTo.insert(std::make_pair(
				buffer, MPTItemAbstractState(identBuffers, false)));
	}
	m_mayPointsTo.insert(std::make_pair("null",
			MPTItemAbstractState({"null"}, false)));
}

bool MPTAbstractState::join(const MPTAbstractState & other) {
	bool isChanged = false;
	for (auto & otherpt : other.m_mayPointsTo) {
		std::string name = otherpt.first;
		const MPTItemAbstractState & buffers = otherpt.second;
		auto pt_it = m_mayPointsTo.find(name);
		if (pt_it == m_mayPointsTo.end()) {
			isChanged = true;
			m_mayPointsTo.insert(std::make_pair(name,
					MPTItemAbstractState(buffers.getBuffers(), true)));
		} else {
			isChanged = pt_it->second.join(buffers) || isChanged;
		}
	}
	return isChanged;
}

bool MPTAbstractState::meet(const MPTAbstractState & other) {
	bool isChanged = false;
	for (std::map<std::string, MPTItemAbstractState >::iterator it = m_mayPointsTo.begin(),
			ie = m_mayPointsTo.end();
			it != ie; it++) {
		const std::string & name = it->first;
		MPTItemAbstractState & buffers = it->second;
		auto pt_it = other.m_mayPointsTo.find(name);
		if (pt_it == other.m_mayPointsTo.end()) {
			// delete it
			it = m_mayPointsTo.erase(it);
			isChanged = true;
		} else {
			// intersect
			const MPTItemAbstractState & other_buffers = pt_it->second;
			isChanged = buffers.meet(other_buffers) || isChanged;
		}
	}
	return isChanged;
}

void MPTAbstractState::forget(const std::string & name) {
	std::map<std::string, MPTItemAbstractState>::iterator it = m_mayPointsTo.find(name);
	if (it == m_mayPointsTo.end()) {
		return;
	}
	assert(it->second.isWritable());
	m_mayPointsTo.erase(it);
	assert(m_mayPointsTo.find(name) == m_mayPointsTo.end());
}

MPTItemAbstractState & MPTAbstractState::extend(const std::string& name) {
	return m_mayPointsTo[name];
}

void MPTAbstractState::clear() {
	for (std::map<std::string, MPTItemAbstractState >::iterator it = m_mayPointsTo.begin(),
	                                                            ie = m_mayPointsTo.end();
			it != ie; it++) {
		MPTItemAbstractState & item = it->second;
		if (item.isWritable()) {
			it = m_mayPointsTo.erase(it);
		}
	}
}

MPTItemAbstractState * MPTAbstractState::find(const std::string& name) {
	std::map<std::string, MPTItemAbstractState>::iterator it = m_mayPointsTo.find(name);
	if (it == m_mayPointsTo.end()) {
		return 0;
	}
	return &it->second;
}

bool MPTAbstractState::isBottom() const {
	return m_mayPointsTo.empty();
}

bool MPTAbstractState::operator==(const MPTAbstractState& other) const {
	return (m_mayPointsTo == other.m_mayPointsTo);
}

bool MPTAbstractState::operator!=(const MPTAbstractState& other) const {
	return !(*this == other);
}

#ifndef MPT_ABSTRACT_STATE_H
#define MPT_ABSTRACT_STATE_H

#include <map>
#include <set>
#include <string>
#include <vector>

class MPTItemAbstractState {
	std::set<std::string> m_buffers;
	bool m_isWritable;
public:
	MPTItemAbstractState();
	MPTItemAbstractState(const std::set<std::string> & buffers, bool isWritable=true);
	const std::set<std::string> & getBuffers() const;
	void insert(std::string buffer);
	void erase(std::string buffer);
	void clear();
	std::set<std::string>::const_iterator begin() const;
	std::set<std::string>::const_iterator end() const;
	bool join(const MPTItemAbstractState & other);
	bool meet(const MPTItemAbstractState & other);
	void lock();
	bool isProvablyNull() const;
	bool isProvablyKernel() const;
	bool empty() const;
	unsigned count() const;
	bool isWritable() const;
	bool contains(const std::string & name) const;
	bool operator==(const MPTItemAbstractState & other) const;
	bool operator!=(const MPTItemAbstractState & other) const;
	MPTItemAbstractState & operator=(const MPTItemAbstractState & other);

	static void updateToIntersection(MPTItemAbstractState & left, MPTItemAbstractState & right);
};

	/*******************************************************************/
	/* pt is a mapping between pointers, and the buffers they *may*    */
	/* point to. The buffers are either user buffers (passed e.g. via  */
	/* arguments), "kernel" for any kernel pointer, "null" for the     */
	/* null pointer, or "user" for any pointer to userspace that is    */
	/* not recognised as a buffer (This shouldn't really happen).      */
	/* Let us use this code for reference                              */
	/*                                                                 */
	/* int f9(__user char *buf1, __user char *buf2, int size)          */
	/* {                                                               */
	/*     char *p = NULL;                                             */
	/*     if (size > 32)                                              */
	/*     {                                                           */
	/*         p = buf1 + 3; // this is (*)                            */
	/*     }                                                           */
	/*     else                                                        */
	/*     {                                                           */
	/*         p = buf2; // and this is (**)                           */
	/*     }                                                           */
	/* }                                                               */
	/* --------------------------------------------------------------- */
	/* the pointer p in line (*) is updated with                       */
	/*                                                                 */
	/* map[p] = {buf1}                                                 */
	/* Additionally, and offset(p,buf1) <= 3 constraint should be      */
	/* added to the numerical part of the AbstractState                */
	/*                                                                 */
	/* --------------------------------------------------------------- */
	/*                                                                 */
	/* the pointer p in line (**) is updated with                      */
	/*                                                                 */
	/* map[p] = buf2                                                   */
	/* Additionally, and offset(p,buf2) <= 0 constraint should be      */
	/* added to the numerical part of the AbstractState                */
	/*                                                                 */
	/* --------------------------------------------------------------- */
	/*                                                                 */
	/* the join between (*) and (**) should then be:                   */
	/*                                                                 */
	/* map[p] = {buf1,buf2}                                            */
	/* The join also joins the constraints on offset(p,buf1) and       */
	/* offset(p, buf2)                                                 */
	/*                                                                 */
	/*******************************************************************/
class MPTAbstractState {
public:
	std::map<std::string, MPTItemAbstractState > m_mayPointsTo;

	MPTAbstractState();
	MPTAbstractState(std::vector<std::string> buffers);

	bool join(const MPTAbstractState & other);
	bool meet(const MPTAbstractState & other);
	void forget(const std::string & pointer);
	void clear();

	MPTItemAbstractState * find(const std::string& name);
	MPTItemAbstractState & extend(const std::string& name);

	virtual bool isBottom() const;
	bool operator==(const MPTAbstractState& other) const;
	bool operator!=(const MPTAbstractState& other) const;
};

template <class stream>
stream & operator<<(stream & s, const MPTItemAbstractState & mpti) {
	for (const std::string & ptr : mpti) {
		s << ptr << ",";
	}
	return s;
}
#endif // MPT_ABSTRACT_STATE_H

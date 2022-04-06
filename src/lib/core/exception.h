#ifndef TARANTOOL_LIB_CORE_EXCEPTION_H_INCLUDED
#define TARANTOOL_LIB_CORE_EXCEPTION_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <trivia/util.h>
#include <stdarg.h>
#include <assert.h>

#include "reflection.h"
#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern const struct type_info type_Exception;
extern const struct type_info type_OutOfMemory;
extern const struct type_info type_FiberIsCancelled;
extern const struct type_info type_TimedOut;
extern const struct type_info type_ChannelIsClosed;
extern const struct type_info type_LuajitError;
extern const struct type_info type_IllegalParams;
extern const struct type_info type_SystemError;
extern const struct type_info type_CollationError;
extern const struct type_info type_SwimError;
extern const struct type_info type_CryptoError;
extern const struct type_info type_RaftError;

const char *
exception_get_string(struct error *e, const struct method_info *method);

int
exception_get_int(struct error *e, const struct method_info *method);

#if defined(__cplusplus)
} /* extern "C" */

class Exception: public error {
public:
	void *operator new(size_t size);
	void *operator new(size_t size, void *p) { (void) size; return p; }
	void operator delete(void*);

	const char *get_file() const { return file; }
	int get_line() const { return line; }
	const char *get_errmsg() const { return errmsg; }

	NORETURN virtual void raise() = 0;
	virtual void log() const;
	/** Duplicate an exception by using its copy constructor. */
	virtual Exception *dup() const = 0;
	virtual ~Exception();

	Exception& operator=(const Exception&) = delete;
protected:
	/** Copy constructor for Exception object. */
	Exception(const Exception &other) { error_create_copy(this, &other); }
	Exception(const struct type_info *type, const char *file, unsigned line);
};

class SystemError: public Exception {
public:
	virtual void raise() { throw this; }

	SystemError(const char *file, unsigned line,
		    const char *format, ...);

	SystemError()
		:Exception(&type_SystemError, NULL, 0)
	{
	}

	SystemError(const SystemError & other) : Exception(other) {}

	virtual Exception *dup() const { return new SystemError(*this); }
protected:
	SystemError(const struct type_info *type, const char *file, unsigned line);
};

extern const struct type_info type_SocketError;
class SocketError: public SystemError {
public:
	SocketError(const char *file, unsigned line, const char *socketname,
		    const char *format, ...);

	SocketError()
		:SystemError(&type_SocketError, NULL, 0)
	{
	}

	SocketError(const SocketError & other) : SystemError(other) {}

	virtual Exception *dup() const { return new SocketError(*this); }

	virtual void raise()
	{
		throw this;
	}
};

class OutOfMemory: public SystemError {
public:
	OutOfMemory(const char *file, unsigned line,
		    size_t amount, const char *allocator,
		    const char *object);

	OutOfMemory()
		:SystemError(&type_OutOfMemory, NULL, 0)
	{
	}

	OutOfMemory(const OutOfMemory & other) : SystemError(other) {}

	virtual Exception *dup() const { return new OutOfMemory(*this); }

	virtual void raise() { throw this; }
};

class TimedOut: public SystemError {
public:
	TimedOut(const char *file, unsigned line);

	TimedOut()
		:SystemError(&type_TimedOut, NULL, 0)
	{
	}

	TimedOut(const TimedOut & other) : SystemError(other) {}

	virtual Exception *dup() const { return new TimedOut(*this); }

	virtual void raise() { throw this; }
};

class ChannelIsClosed: public Exception {
public:
	ChannelIsClosed(const char *file, unsigned line);

	ChannelIsClosed()
		:Exception(&type_ChannelIsClosed, NULL, 0)
	{
	}

	ChannelIsClosed(const ChannelIsClosed & other) : Exception(other) {}

	virtual Exception *dup() const { return new ChannelIsClosed(*this); }

	virtual void raise() { throw this; }
};

/**
 * This is thrown by fiber_* API calls when the fiber is
 * cancelled.
 */
class FiberIsCancelled: public Exception {
public:
	FiberIsCancelled(const char *file, unsigned line);

	FiberIsCancelled()
		:Exception(&type_FiberIsCancelled, NULL, 0)
	{
	}

	FiberIsCancelled(const FiberIsCancelled & other) : Exception(other) {}

	virtual Exception *dup() const { return new FiberIsCancelled(*this); }

	virtual void log() const;
	virtual void raise() { throw this; }
};

class LuajitError: public Exception {
public:
	LuajitError(const char *file, unsigned line,
		    const char *msg);

	LuajitError()
		:Exception(&type_LuajitError, NULL, 0)
	{
	}

	LuajitError(const LuajitError & other) : Exception(other) {}

	virtual Exception *dup() const { return new LuajitError(*this); }

	virtual void raise() { throw this; }
};

class IllegalParams: public Exception {
public:
	IllegalParams(const char *file, unsigned line, const char *format, ...);

	IllegalParams()
		:Exception(&type_IllegalParams, NULL, 0)
	{
	}

	IllegalParams(const IllegalParams & other) : Exception(other) {}

	virtual Exception *dup() const { return new IllegalParams(*this); }

	virtual void raise() { throw this; }
};

class CollationError: public Exception {
public:
	CollationError(const char *file, unsigned line, const char *format,
		       ...);

	CollationError()
		:Exception(&type_CollationError, NULL, 0)
	{
	}

	CollationError(const CollationError & other) : Exception(other) {}

	virtual Exception *dup() const { return new CollationError(*this); }

	virtual void raise() { throw this; }
};

class SwimError: public Exception {
public:
	SwimError(const char *file, unsigned line, const char *format, ...);

	SwimError()
		:Exception(&type_SwimError, NULL, 0)
	{
	}

	SwimError(const SwimError & other) : Exception(other) {}

	virtual Exception *dup() const { return new SwimError(*this); }

	virtual void raise() { throw this; }
};

class CryptoError: public Exception {
public:
	CryptoError(const char *file, unsigned line, const char *format, ...);

	CryptoError()
		:Exception(&type_CryptoError, NULL, 0)
	{
	}

	CryptoError(const CryptoError & other) : Exception(other) {}

	virtual Exception *dup() const { return new CryptoError(*this); }

	virtual void raise() { throw this; }
};

class RaftError: public Exception {
public:
	RaftError(const char *file, unsigned line, const char *format, ...);
	RaftError(const RaftError & other) : Exception(other) {}
	virtual Exception *dup() const { return new RaftError(*this); }
	virtual void raise() { throw this; }
};

/**
 * Initialize the exception subsystem.
 */
void
exception_init();

#define tnt_error(class, ...) ({					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	class *e = new class(__FILE__, __LINE__, ##__VA_ARGS__);	\
	diag_set_error(diag_get(), e);					\
	e;								\
})

#define tnt_raise(...) do {						\
	throw tnt_error(__VA_ARGS__);					\
} while (0)

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_EXCEPTION_H_INCLUDED */

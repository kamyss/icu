/*
*******************************************************************************
*
*   Copyright (C) 1999-2004, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  unistr_case.cpp
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:2
*
*   created on: 2004aug19
*   created by: Markus W. Scherer
*
*   Case-mapping functions moved here from unistr.cpp
*/

#include "unicode/utypes.h"
#include "unicode/putil.h"
#include "unicode/locid.h"
#include "cstring.h"
#include "cmemory.h"
#include "unicode/ustring.h"
#include "unicode/unistr.h"
#include "unicode/uchar.h"
#include "unicode/ubrk.h"
#include "ustr_imp.h"
#include "unormimp.h"

U_NAMESPACE_BEGIN

//========================================
// Read-only implementation
//========================================

int8_t
UnicodeString::doCaseCompare(int32_t start,
                             int32_t length,
                             const UChar *srcChars,
                             int32_t srcStart,
                             int32_t srcLength,
                             uint32_t options) const
{
  // compare illegal string values
  // treat const UChar *srcChars==NULL as an empty string
  if(isBogus()) {
    return -1;
  }

  // pin indices to legal values
  pinIndices(start, length);

  if(srcChars == NULL) {
    srcStart = srcLength = 0;
  }

  // get the correct pointer
  const UChar *chars = getArrayStart();

  chars += start;
  srcChars += srcStart;

  if(chars != srcChars) {
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t result=u_strcmpFold(chars, length, srcChars, srcLength,
                                options|U_COMPARE_IGNORE_CASE, &errorCode);
    if(result!=0) {
      return (int8_t)(result >> 24 | 1);
    }
  } else {
    // get the srcLength if necessary
    if(srcLength < 0) {
      srcLength = u_strlen(srcChars + srcStart);
    }
    if(length != srcLength) {
      return (int8_t)((length - srcLength) >> 24 | 1);
    }
  }
  return 0;
}

//========================================
// Write implementation
//========================================

/*
 * Implement argument checking and buffer handling
 * for string case mapping as a common function.
 */
enum {
    TO_LOWER,
    TO_UPPER,
    TO_TITLE,
    FOLD_CASE
};

UnicodeString &
UnicodeString::toLower() {
  return caseMap(0, Locale::getDefault(), 0, TO_LOWER);
}

UnicodeString &
UnicodeString::toLower(const Locale &locale) {
  return caseMap(0, locale, 0, TO_LOWER);
}

UnicodeString &
UnicodeString::toUpper() {
  return caseMap(0, Locale::getDefault(), 0, TO_UPPER);
}

UnicodeString &
UnicodeString::toUpper(const Locale &locale) {
  return caseMap(0, locale, 0, TO_UPPER);
}

#if !UCONFIG_NO_BREAK_ITERATION

UnicodeString &
UnicodeString::toTitle(BreakIterator *titleIter) {
  return caseMap(titleIter, Locale::getDefault(), 0, TO_TITLE);
}

UnicodeString &
UnicodeString::toTitle(BreakIterator *titleIter, const Locale &locale) {
  return caseMap(titleIter, locale, 0, TO_TITLE);
}

#endif

UnicodeString &
UnicodeString::foldCase(uint32_t options) {
    /* The Locale parameter isn't used.
    We pick a random non-case specific locale that is created cheaply.
    */
    return caseMap(0, Locale::getEnglish(), options, FOLD_CASE);
}

UnicodeString &
UnicodeString::caseMap(BreakIterator *titleIter,
                       const Locale& locale,
                       uint32_t options,
                       int32_t toWhichCase) {
  if(fLength <= 0) {
    // nothing to do
    return *this;
  }

  UErrorCode errorCode;

  errorCode = U_ZERO_ERROR;
  UCaseProps *csp=ucase_getSingleton(&errorCode);
  if(U_FAILURE(errorCode)) {
    setToBogus();
    return *this;
  }

  // We need to allocate a new buffer for the internal string case mapping function.
  // This is very similar to how doReplace() below keeps the old array pointer
  // and deletes the old array itself after it is done.
  // In addition, we are forcing cloneArrayIfNeeded() to always allocate a new array.
  UChar *oldArray = fArray;
  int32_t oldLength = fLength;
  int32_t *bufferToDelete = 0;

  // Make sure that if the string is in fStackBuffer we do not overwrite it!
  int32_t capacity;
  if(fLength <= US_STACKBUF_SIZE) {
    if(fArray == fStackBuffer) {
      capacity = 2 * US_STACKBUF_SIZE; // make sure that cloneArrayIfNeeded() allocates a new buffer
    } else {
      capacity = US_STACKBUF_SIZE;
    }
  } else {
    capacity = fLength + 20;
  }
  if(!cloneArrayIfNeeded(capacity, capacity, FALSE, &bufferToDelete, TRUE)) {
    return *this;
  }

#if !UCONFIG_NO_BREAK_ITERATION
  // set up the titlecasing break iterator
  UBreakIterator *cTitleIter = 0;

  if(toWhichCase == TO_TITLE) {
    errorCode = U_ZERO_ERROR;
    if(titleIter != 0) {
      cTitleIter = (UBreakIterator *)titleIter;
      ubrk_setText(cTitleIter, oldArray, oldLength, &errorCode);
    } else {
      cTitleIter = ubrk_open(UBRK_WORD, locale.getName(),
                             oldArray, oldLength,
                             &errorCode);
    }
    if(U_FAILURE(errorCode)) {
      uprv_free(bufferToDelete);
      setToBogus();
      return *this;
    }
  }
#endif

  // Case-map, and if the result is too long, then reallocate and repeat.
  do {
    errorCode = U_ZERO_ERROR;
    if(toWhichCase==TO_LOWER) {
      fLength = ustr_toLower(csp, fArray, fCapacity,
                             oldArray, oldLength,
                             locale.getName(), &errorCode);
    } else if(toWhichCase==TO_UPPER) {
      fLength = ustr_toUpper(csp, fArray, fCapacity,
                             oldArray, oldLength,
                             locale.getName(), &errorCode);
    } else if(toWhichCase==TO_TITLE) {
#if UCONFIG_NO_BREAK_ITERATION
        errorCode=U_UNSUPPORTED_ERROR;
#else
      fLength = ustr_toTitle(csp, fArray, fCapacity,
                             oldArray, oldLength,
                             cTitleIter, locale.getName(), &errorCode);
#endif
    } else {
      fLength = ustr_foldCase(csp, fArray, fCapacity,
                              oldArray, oldLength,
                              options,
                              &errorCode);
    }
  } while(errorCode==U_BUFFER_OVERFLOW_ERROR && cloneArrayIfNeeded(fLength, fLength, FALSE));

#if !UCONFIG_NO_BREAK_ITERATION
  if(cTitleIter != 0 && titleIter == 0) {
    ubrk_close(cTitleIter);
  }
#endif

  if (bufferToDelete) {
    uprv_free(bufferToDelete);
  }
  if(U_FAILURE(errorCode)) {
    setToBogus();
  }
  return *this;
}

U_NAMESPACE_END

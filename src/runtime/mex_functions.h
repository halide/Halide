// This file intentionally does not use include guards!!
// The intended usage of this file is to define MEX_FN to do something
// useful with a mex function, and then include this file. This file #undefs
// MEX_FN after it is done. This file contains 3 types of functions:
//
// - MEX_FN(ret, func, args): A function with return type 'ret', name 'func',
//   and arguments 'args'.
// - MEX_FN_700(ret, func, func_700, args): Similar to MEX_FN, but func_700 is
//   the name of the function with _700 appended. This is only used for
//   Matlab 7.0 API functions.
// - MEX_FN_730(ret, func, func_730, args): Similar to MEX_FN_700, but for the
//   Matlab 7.3 API.

// Provide default no-op definitions for the 3 macros if they don't already
// exist.
#ifndef MEX_FN
#define MEX_FN(ret, func, args)
#endif

#ifndef MEX_FN_730
#define MEX_FN_730(ret, func, func_730, args) MEX_FN(ret, func_730, args)
#endif

#ifndef MEX_FN_700
#define MEX_FN_700(ret, func, func_700, args) MEX_FN(ret, func_700, args)
#endif

// mex.h
//MEX_FN(int, mexPrintf, (const char*, ...));
//MEX_FN(void, mexErrMsgTxt, (const char*));
//MEX_FN(void, mexErrMsgIdAndTxt, (const char *, const char*, ...));
MEX_FN(void, mexWarnMsgTxt, (const char*));
//MEX_FN(void, mexWarnMsgIdAndTxt, (const char *, const char*, ...));
//MEX_FN(void, mexMakeArrayPersistent, (const mxArray*));
//MEX_FN(void, mexMakeMemoryPersistent, (void *ptr));
//MEX_FN(int, mexSet, (double, const char*, mxArray*));
//MEX_FN(const mxArray*, mexGet, (double, const char*));
//MEX_FN(int, mexCallMATLAB, (int, mxArray**, int, const mxArray**, const char *));
//MEX_FN(mxArray*, mexCallMATLABWithTrap, (int, mxArray**, int, const mxArray**, const char *));
//MEX_FN(void, mexSetTrapFlag, (int));
//MEX_FN(void, mexPrintAssertion, (const char*, const char*, int, const char*));
//MEX_FN(bool, mexIsGlobal, (const mxArray*));
//MEX_FN(int, mexPutVariable, (const char*, const char*, const mxArray*));
//MEX_FN(const mxArray*, mexGetVariablePtr, (const char*, const char*));
//MEX_FN(mxArray*, mexGetVariable, (const char*, const char*));
//MEX_FN(void, mexLock, (void));
//MEX_FN(void, mexUnlock, (void));
//MEX_FN(bool, mexIsLocked, (void));
//MEX_FN(const char*, mexFunctionName, (void));
//MEX_FN(int, mexEvalString, (const char*));
//MEX_FN(mxArray*, mexEvalStringWithTrap, (const char*));
//MEX_FN(int, mexAtExit, (mex_exit_fn));

// matrix.h
//MEX_FN(void*, mxMalloc, (size_t));
//MEX_FN(void*, mxCalloc, (size_t, size_t));
//MEX_FN(void, mxFree, (void*));
//MEX_FN(void*, mxRealloc, (void*, size_t));
MEX_FN_730(size_t, mxGetNumberOfDimensions, mxGetNumberOfDimensions_730, (const mxArray*));
MEX_FN_700(int, mxGetNumberOfDimensions, mxGetNumberOfDimensions_700, (const mxArray*));
MEX_FN_730(const size_t*, mxGetDimensions, mxGetDimensions_730, (const mxArray*));
MEX_FN_700(const int*, mxGetDimensions, mxGetDimensions_700, (const mxArray*));
//MEX_FN(size_t, mxGetM, (const mxArray*));
//MEX_FN_730(size_t*, mxGetIr, mxGetIr_730, (const mxArray*));
//MEX_FN_700(int*, mxGetIr, mxGetIr_700, (const mxArray*));
//MEX_FN_730(size_t*, mxGetJc, mxGetJc_730, (const mxArray*));
//MEX_FN_700(int*, mxGetJc, mxGetJc_700, (const mxArray*));
//MEX_FN_730(size_t, mxGetNzmax, mxGetNzmax_730, (const mxArray*));
//MEX_FN_700(int, mxGetNzmax, mxGetNzmax_700, (const mxArray*));
//MEX_FN_730(void, mxSetNzmax, mxSetNzmax_730, (mxArray*, size_t));
//MEX_FN_700(void, mxSetNzmax, mxSetNzmax_700, (mxArray*, int));
//MEX_FN(const char*, mxGetFieldNameByNumber, (const mxArray*, int));
//MEX_FN_730(mxArray*, mxGetFieldByNumber, mxGetFieldByNumber_730, (const mxArray*, size_t, int));
//MEX_FN_700(mxArray*, mxGetFieldByNumber, mxGetFieldByNumber_700, (const mxArray*, int, int));
//MEX_FN_730(mxArray*, mxGetCell, mxGetCell_730, (const mxArray*, size_t));
//MEX_FN_700(mxArray*, mxGetCell, mxGetCell_700, (const mxArray*, int));
MEX_FN(mxClassID, mxGetClassID, (const mxArray*));
MEX_FN(void*, mxGetData, (const mxArray*));
//MEX_FN(void, mxSetData, (mxArray*,void*));
MEX_FN(bool, mxIsNumeric, (const mxArray*));
//MEX_FN(bool, mxIsCell, (const mxArray*));
MEX_FN(bool, mxIsLogical, (const mxArray*));
//MEX_FN(bool, mxIsChar, (const mxArray*));
//MEX_FN(bool, mxIsStruct, (const mxArray*));
//MEX_FN(bool, mxIsOpaque, (const mxArray*));
//MEX_FN(bool, mxIsFunctionHandle, (const mxArray*));
//MEX_FN(bool, mxIsObject, (const mxArray*));
//MEX_FN(void*, mxGetImagData, (const mxArray*));
//MEX_FN(void, mxSetImagData, (mxArray*, void*));
MEX_FN(bool, mxIsComplex, (const mxArray*));
//MEX_FN(bool, mxIsSparse, (const mxArray*));
//MEX_FN(bool, mxIsDouble, (const mxArray*));
//MEX_FN(bool, mxIsSingle, (const mxArray*));
//MEX_FN(bool, mxIsInt8, (const mxArray*));
//MEX_FN(bool, mxIsUint8, (const mxArray*));
//MEX_FN(bool, mxIsInt16, (const mxArray*));
//MEX_FN(bool, mxIsUint16, (const mxArray*));
//MEX_FN(bool, mxIsInt32, (const mxArray*));
//MEX_FN(bool, mxIsUint32, (const mxArray*));
//MEX_FN(bool, mxIsInt64, (const mxArray*));
//MEX_FN(bool, mxIsUint64, (const mxArray*));
//MEX_FN(size_t, mxGetNumberOfElements, (const mxArray*));
//MEX_FN(double*, mxGetPr, (const mxArray*));
//MEX_FN(void, mxSetPr, (mxArray*, double*));
//MEX_FN(double*, mxGetPi, (const mxArray*));
//MEX_FN(void, mxSetPi, (mxArray*, double*));
//MEX_FN(mxChar*, mxGetChars, (const mxArray*));
//MEX_FN(int, mxGetUserBits, (const mxArray*));
//MEX_FN(void, mxSetUserBits, (mxArray*, int));
MEX_FN(double, mxGetScalar, (const mxArray*));
//MEX_FN(bool, mxIsFromGlobalWS, (const mxArray*));
//MEX_FN(void, mxSetFromGlobalWS, (mxArray*, bool));
//MEX_FN_730(void, mxSetM, mxSetM_730, (mxArray*, size_t));
//MEX_FN_700(void, mxSetM, mxSetM_700, (mxArray*, int));
//MEX_FN(size_t, mxGetN, (const mxArray*));
//MEX_FN(bool, mxIsEmpty, (const mxArray*));
//MEX_FN(int, mxGetFieldNumber, (const mxArray*, const char*));
//MEX_FN_730(void, mxSetIr, mxSetIr_730, (mxArray*, size_t*));
//MEX_FN_700(void, mxSetIr, mxSetIr_700, (mxArray*, int*));
//MEX_FN_730(void, mxSetJc, mxSetJc_730, (mxArray*, size_t*));
//MEX_FN_700(void, mxSetJc, mxSetJc_700, (mxArray*, int*));
MEX_FN(size_t, mxGetElementSize, (const mxArray*));
//MEX_FN_730(size_t, mxCalcSingleSubscript, mxCalcSingleSubscript_730, (const mxArray*, size_t, const size_t*));
//MEX_FN_700(int, mxCalcSingleSubscript, mxCalcSingleSubscript_700, (const mxArray*, int, const int*));
//MEX_FN(int, mxGetNumberOfFields, (const mxArray*));
//MEX_FN_730(void, mxSetCell, mxSetCell_730, (mxArray*, size_t, mxArray*));
//MEX_FN_700(void, mxSetCell, mxSetCell_700, (mxArray*, int, mxArray*));
//MEX_FN_730(void, mxSetFieldByNumber, mxSetFieldByNumber_730, (mxArray*, size_t, int, mxArray*));
//MEX_FN_700(void, mxSetFieldByNumber, mxSetFieldByNumber_700, (mxArray*, int, int, mxArray*));
//MEX_FN_730(mxArray*, mxGetField, mxGetField_730, (const mxArray*, size_t, const char*));
//MEX_FN_700(mxArray*, mxGetField, mxGetField_700, (const mxArray*, int, const char*));
//MEX_FN_730(void, mxSetField, mxSetField_730, (mxArray*, size_t, const char*, mxArray*));
//MEX_FN_700(void, mxSetField, mxSetField_700, (mxArray*, int, const char*, mxArray*));
//MEX_FN_730(mxArray*, mxGetProperty, mxGetProperty_730, (const mxArray*, const size_t, const char*));
//MEX_FN_700(mxArray*, mxGetProperty, mxGetProperty_700, (const mxArray*, const int, const char*));
//MEX_FN_730(void, mxSetProperty, mxSetProperty_730, (mxArray*, size_t, const char*, const mxArray*));
//MEX_FN_700(void, mxSetProperty, mxSetProperty_700, (mxArray*, int, const char*, const mxArray*));
//MEX_FN(const char*, mxGetClassName, (const mxArray*));
//MEX_FN(bool, mxIsClass, (const mxArray*, const char*));
MEX_FN_730(mxArray*, mxCreateNumericMatrix, mxCreateNumericMatrix_730, (size_t, size_t, mxClassID, mxComplexity));
MEX_FN_700(mxArray*, mxCreateNumericMatrix, mxCreateNumericMatrix_700, (int, int, mxClassID, mxComplexity));
//MEX_FN_730(void, mxSetN, mxSetN_730, (mxArray*, size_t));
//MEX_FN_700(void, mxSetN, mxSetN_700, (mxArray*, int));
//MEX_FN_730(int, mxSetDimensions, mxSetDimensions_730, (mxArray*, const size_t*, size_t));
//MEX_FN_700(int, mxSetDimensions, mxSetDimensions_700, (mxArray*, const int*, int));
//MEX_FN(void, mxDestroyArray, (mxArray*));
//MEX_FN_730(mxArray*, mxCreateNumericArray, mxCreateNumericArray_730, (size_t, const size_t*, mxClassID, mxComplexity));
//MEX_FN_700(mxArray*, mxCreateNumericArray, mxCreateNumericArray_700, (int, const int*, mxClassID, mxComplexity));
//MEX_FN_730(mxArray*, mxCreateCharArray, mxCreateCharArray_730, (size_t, const size_t*));
//MEX_FN_700(mxArray*, mxCreateCharArray, mxCreateCharArray_700, (int, const int*));
//MEX_FN_730(mxArray*, mxCreateDoubleMatrix, mxCreateDoubleMatrix_730, (size_t, size_t, mxComplexity));
//MEX_FN_700(mxArray*, mxCreateDoubleMatrix, mxCreateDoubleMatrix_700, (int, int, mxComplexity));
//MEX_FN(mxLogical*, mxGetLogicals, (const mxArray*));
//MEX_FN_730(mxArray*, mxCreateLogicalArray, mxCreateLogicalArray_730, (size_t, const size_t*));
//MEX_FN_700(mxArray*, mxCreateLogicalArray, mxCreateLogicalArray_700, (int, const int*));
//MEX_FN_730(mxArray*, mxCreateLogicalMatrix, mxCreateLogicalMatrix_730, (size_t, size_t));
//MEX_FN_700(mxArray*, mxCreateLogicalMatrix, mxCreateLogicalMatrix_700, (int, int));
//MEX_FN(mxArray*, mxCreateLogicalScalar, (bool));
//MEX_FN(bool, mxIsLogicalScalar, (const mxArray*));
//MEX_FN(bool, mxIsLogicalScalarTrue, (const mxArray*));
//MEX_FN(mxArray*, mxCreateDoubleScalar, (double));
//MEX_FN_730(mxArray*, mxCreateSparse, mxCreateSparse_730, (size_t, size_t, size_t, mxComplexity));
//MEX_FN_700(mxArray*, mxCreateSparse, mxCreateSparse_700, (int, int, int, mxComplexity));
//MEX_FN_730(mxArray*, mxCreateSparseLogicalMatrix, mxCreateSparseLogicalMatrix_730, (size_t, size_t, size_t));
//MEX_FN_700(mxArray*, mxCreateSparseLogicalMatrix, mxCreateSparseLogicalMatrix_700, (int, int, int));
//MEX_FN_730(void, mxGetNChars, mxGetNChars_730, (const mxArray*, char*, size_t));
//MEX_FN_700(void, mxGetNChars, mxGetNChars_700, (const mxArray*, char*, int));
//MEX_FN_730(int, mxGetString, mxGetString_730, (const mxArray*, char*, size_t));
//MEX_FN_700(int, mxGetString, mxGetString_700, (const mxArray*, char*, int));
//MEX_FN(char*, mxArrayToString, (const mxArray*));
//MEX_FN_730(mxArray*, mxCreateStringFromNChars, mxCreateStringFromNChars_730, (const char*, size_t));
//MEX_FN_700(mxArray*, mxCreateStringFromNChars, mxCreateStringFromNChars_700, (const char*, int));
//MEX_FN(mxArray*, mxCreateString, (const char*));
//MEX_FN_730(mxArray*, mxCreateCharMatrixFromStrings, mxCreateCharMatrixFromStrings_730, (size_t, const char**));
//MEX_FN_700(mxArray*, mxCreateCharMatrixFromStrings, mxCreateCharMatrixFromStrings_700, (int, const char**));
//MEX_FN_730(mxArray*, mxCreateCellMatrix, mxCreateCellMatrix_730, (size_t, size_t));
//MEX_FN_700(mxArray*, mxCreateCellMatrix, mxCreateCellMatrix_700, (int, int));
//MEX_FN_730(mxArray*, mxCreateCellArray, mxCreateCellArray_730, (size_t, const size_t*));
//MEX_FN_700(mxArray*, mxCreateCellArray, mxCreateCellArray_700, (int, const int*));
//MEX_FN_730(mxArray*, mxCreateStructMatrix, mxCreateStructMatrix_730, (size_t, size_t, int, const char**));
//MEX_FN_700(mxArray*, mxCreateStructMatrix, mxCreateStructMatrix_700, (int, int, int, const char**));
//MEX_FN_730(mxArray*, mxCreateStructArray, mxCreateStructArray_730, (size_t, const size_t*, int, const char**));
//MEX_FN_700(mxArray*, mxCreateStructArray, mxCreateStructArray_700, (int, const int*, int, const char**));
//MEX_FN(mxArray*, mxDuplicateArray, (const mxArray*));
//MEX_FN(int, mxSetClassName, (mxArray*, const char*));
//MEX_FN(int, mxAddField, (mxArray*, const char*));
//MEX_FN(void, mxRemoveField, (mxArray*, int));
//MEX_FN(double, mxGetEps, (void));
//MEX_FN(double, mxGetInf, (void));
//MEX_FN(double, mxGetNaN, (void));
//MEX_FN(bool, mxIsFinite, (double));
//MEX_FN(bool, mxIsInf, (double));
//MEX_FN(bool, mxIsNaN, (double));

#ifdef MEX_FN
#undef MEX_FN
#endif
#ifdef MEX_FN_730
#undef MEX_FN_730
#endif
#ifdef MEX_FN_700
#undef MEX_FN_700
#endif

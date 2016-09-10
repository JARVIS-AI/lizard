#define OPTIMAL_ML (int)((ML_MASK_LZ4-1)+MINMATCH)

/* Update chains up to ip (excluded) */
FORCE_INLINE void LZ5_Insert (LZ5_stream_t* ctx, const BYTE* ip)
{
    U32* const chainTable = ctx->chainTable;
    U32* const hashTable  = ctx->hashTable;
    const BYTE* const base = ctx->base;
    U32 const target = (U32)(ip - base);
    U32 idx = ctx->nextToUpdate;
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;

    while (idx < target) {
        size_t const h = LZ5_hash4Ptr(base+idx, hashLog);
        size_t delta = idx - hashTable[h];
        if (delta>maxDistance) delta = maxDistance;
        DELTANEXT(idx) = (U32)delta;
        hashTable[h] = idx;
        idx++;
    }

    ctx->nextToUpdate = target;
}


FORCE_INLINE int LZ5_InsertAndFindBestMatch (LZ5_stream_t* ctx,   /* Index table will be updated */
                                               const BYTE* ip, const BYTE* const iLimit,
                                               const BYTE** matchpos)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const BYTE* const dictBase = ctx->dictBase;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const BYTE* const dictEnd = dictBase + dictLimit;
    U32 matchIndex, delta;
    const BYTE* match;
    int nbAttempts=ctx->params.searchNum;
    size_t ml=0;
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;
    const U32 lowLimit = (ctx->lowLimit + maxDistance >= (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - maxDistance;

    /* HC4 match finder */
    LZ5_Insert(ctx, ip);
    matchIndex = HashTable[LZ5_hash4Ptr(ip, hashLog)];

    while ((matchIndex>=lowLimit) && (nbAttempts)) {
        nbAttempts--;
        if (matchIndex >= dictLimit) {
            match = base + matchIndex;
            if (*(match+ml) == *(ip+ml)
                && (MEM_read32(match) == MEM_read32(ip)))
            {
                size_t const mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
                if (mlt > ml) { ml = mlt; *matchpos = match; }
            }
        } else {
            match = dictBase + matchIndex;
//            fprintf(stderr, "dictBase[%p]+matchIndex[%d]=match[%p] dictLimit=%d base=%p ip=%p iLimit=%p off=%d\n", dictBase, matchIndex, match, dictLimit, base, ip, iLimit, (U32)(ip-match));
            if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
            if (MEM_read32(match) == MEM_read32(ip)) {
#if 1
                size_t mlt = LZ5_count_2segments(ip+MINMATCH, match+MINMATCH, iLimit, dictEnd, lowPrefixPtr) + MINMATCH;
#else
                size_t mlt;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iLimit) vLimit = iLimit;
                mlt = LZ5_count(ip+MINMATCH, match+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iLimit);
#endif
                if (mlt > ml) { ml = mlt; *matchpos = base + matchIndex; }   /* virtual matchpos */
            }
        }
        delta = DELTANEXT(matchIndex);
        if (delta > matchIndex) break;
        matchIndex -= delta;
    }

    return (int)ml;
}


FORCE_INLINE int LZ5_InsertAndGetWiderMatch (
    LZ5_stream_t* ctx,
    const BYTE* const ip,
    const BYTE* const iLowLimit,
    const BYTE* const iHighLimit,
    int longest,
    const BYTE** matchpos,
    const BYTE** startpos)
{
    U32* const chainTable = ctx->chainTable;
    U32* const HashTable = ctx->hashTable;
    const BYTE* const base = ctx->base;
    const U32 dictLimit = ctx->dictLimit;
    const BYTE* const lowPrefixPtr = base + dictLimit;
    const BYTE* const dictBase = ctx->dictBase;
    const BYTE* const dictEnd = dictBase + dictLimit;
    U32   matchIndex, delta;
    int nbAttempts = ctx->params.searchNum;
    int LLdelta = (int)(ip-iLowLimit);
    const int hashLog = ctx->params.hashLog;
    const U32 contentMask = (1 << ctx->params.contentLog) - 1;
    const U32 maxDistance = (1 << ctx->params.windowLog) - 1;
    const U32 lowLimit = (ctx->lowLimit + maxDistance >= (U32)(ip-base)) ? ctx->lowLimit : (U32)(ip - base) - maxDistance;
    
    /* First Match */
    LZ5_Insert(ctx, ip);
    matchIndex = HashTable[LZ5_hash4Ptr(ip, hashLog)];

    while ((matchIndex>=lowLimit) && (nbAttempts)) {
        nbAttempts--;
        if (matchIndex >= dictLimit) {
            const BYTE* matchPtr = base + matchIndex;
            if (*(iLowLimit + longest) == *(matchPtr - LLdelta + longest)) {
                if (MEM_read32(matchPtr) == MEM_read32(ip)) {
                    int mlt = MINMATCH + LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, iHighLimit);
                    int back = 0;

                    while ((ip+back > iLowLimit)
                           && (matchPtr+back > lowPrefixPtr)
                           && (ip[back-1] == matchPtr[back-1]))
                            back--;

                    mlt -= back;

                    if (mlt > longest) {
                        longest = (int)mlt;
                        *matchpos = matchPtr+back;
                        *startpos = ip+back;
                    }
                }
            }
        } else {
            const BYTE* matchPtr = dictBase + matchIndex;
            if ((U32)((dictLimit-1) - matchIndex) >= 3)  /* intentional overflow */
            if (MEM_read32(matchPtr) == MEM_read32(ip)) {
                int back=0;
#if 1
                size_t mlt = LZ5_count_2segments(ip+MINMATCH, matchPtr+MINMATCH, iHighLimit, dictEnd, lowPrefixPtr) + MINMATCH;
#else
                size_t mlt;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iHighLimit) vLimit = iHighLimit;
                mlt = LZ5_count(ip+MINMATCH, matchPtr+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iHighLimit))
                    mlt += LZ5_count(ip+mlt, base+dictLimit, iHighLimit);
#endif
                while ((ip+back > iLowLimit) && (matchIndex+back > lowLimit) && (ip[back-1] == matchPtr[back-1])) back--;
                mlt -= back;
                if ((int)mlt > longest) { longest = (int)mlt; *matchpos = base + matchIndex + back; *startpos = ip+back; }
            }
        }
        delta = DELTANEXT(matchIndex);
        if (delta > matchIndex) break;
        matchIndex -= delta;
    }

    return longest;
}


FORCE_INLINE int LZ5_compress_HC (
     void* const ctxvoid,
     const char* const source,
     char* const dest,
     const int inputSize,
     const int maxOutputSize,
     const limitedOutput_directive outputLimited)
{
    LZ5_stream_t* ctx = (LZ5_stream_t*) ctxvoid;
    const BYTE* ip = (const BYTE*) source;
    const BYTE* anchor = ip;
    const BYTE* const iend = ip + inputSize;
    const BYTE* const mflimit = iend - MFLIMIT;
    const BYTE* const matchlimit = (iend - LASTLITERALS);

    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + maxOutputSize;

    int   ml, ml2, ml3, ml0;
    const BYTE* ref = NULL;
    const BYTE* start2 = NULL;
    const BYTE* ref2 = NULL;
    const BYTE* start3 = NULL;
    const BYTE* ref3 = NULL;
    const BYTE* start0;
    const BYTE* ref0;

    /* init */
    ctx->end += inputSize;

    ip++;

    /* Main Loop */
    while (ip < mflimit) {
        ml = LZ5_InsertAndFindBestMatch (ctx, ip, matchlimit, (&ref));
        if (!ml) { ip++; continue; }

        /* saved, in case we would skip too much */
        start0 = ip;
        ref0 = ref;
        ml0 = ml;

_Search2:
        if (ip+ml < mflimit)
            ml2 = LZ5_InsertAndGetWiderMatch(ctx, ip + ml - 2, ip + 1, matchlimit, ml, &ref2, &start2);
        else ml2 = ml;

        if (ml2 == ml) { /* No better match */
            if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
            continue;
        }

        if (start0 < ip) {
            if (start2 < ip + ml0) {  /* empirical */
                ip = start0;
                ref = ref0;
                ml = ml0;
            }
        }

        /* Here, start0==ip */
        if ((start2 - ip) < 3) {  /* First Match too small : removed */
            ml = ml2;
            ip = start2;
            ref =ref2;
            goto _Search2;
        }

_Search3:
        /*
        * Currently we have :
        * ml2 > ml1, and
        * ip1+3 <= ip2 (usually < ip1+ml1)
        */
        if ((start2 - ip) < OPTIMAL_ML) {
            int correction;
            int new_ml = ml;
            if (new_ml > OPTIMAL_ML) new_ml = OPTIMAL_ML;
            if (ip+new_ml > start2 + ml2 - MINMATCH) new_ml = (int)(start2 - ip) + ml2 - MINMATCH;
            correction = new_ml - (int)(start2 - ip);
            if (correction > 0) {
                start2 += correction;
                ref2 += correction;
                ml2 -= correction;
            }
        }
        /* Now, we have start2 = ip+new_ml, with new_ml = min(ml, OPTIMAL_ML=18) */

        if (start2 + ml2 < mflimit)
            ml3 = LZ5_InsertAndGetWiderMatch(ctx, start2 + ml2 - 3, start2, matchlimit, ml2, &ref3, &start3);
        else ml3 = ml2;

        if (ml3 == ml2) {  /* No better match : 2 sequences to encode */
            /* ip & ref are known; Now for ml */
            if (start2 < ip+ml)  ml = (int)(start2 - ip);
            /* Now, encode 2 sequences */
            if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
            ip = start2;
            if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, ml2, ref2, outputLimited, oend)) return 0;
            continue;
        }

        if (start3 < ip+ml+3) {  /* Not enough space for match 2 : remove it */
            if (start3 >= (ip+ml)) {  /* can write Seq1 immediately ==> Seq2 is removed, so Seq3 becomes Seq1 */
                if (start2 < ip+ml) {
                    int correction = (int)(ip+ml - start2);
                    start2 += correction;
                    ref2 += correction;
                    ml2 -= correction;
                    if (ml2 < MINMATCH) {
                        start2 = start3;
                        ref2 = ref3;
                        ml2 = ml3;
                    }
                }

                if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;
                ip  = start3;
                ref = ref3;
                ml  = ml3;

                start0 = start2;
                ref0 = ref2;
                ml0 = ml2;
                goto _Search2;
            }

            start2 = start3;
            ref2 = ref3;
            ml2 = ml3;
            goto _Search3;
        }

        /*
        * OK, now we have 3 ascending matches; let's write at least the first one
        * ip & ref are known; Now for ml
        */
        if (start2 < ip+ml) {
            if ((start2 - ip) < (int)ML_MASK_LZ4) {
                int correction;
                if (ml > OPTIMAL_ML) ml = OPTIMAL_ML;
                if (ip + ml > start2 + ml2 - MINMATCH) ml = (int)(start2 - ip) + ml2 - MINMATCH;
                correction = ml - (int)(start2 - ip);
                if (correction > 0) {
                    start2 += correction;
                    ref2 += correction;
                    ml2 -= correction;
                }
            } else {
                ml = (int)(start2 - ip);
            }
        }
        if (LZ5_encodeSequence_LZ4(ctx, &ip, &op, &anchor, ml, ref, outputLimited, oend)) return 0;

        ip = start2;
        ref = ref2;
        ml = ml2;

        start2 = start3;
        ref2 = ref3;
        ml2 = ml3;

        goto _Search3;
    }

    /* Encode Last Literals */
    {   int lastRun = (int)(iend - anchor);
        if ((outputLimited) && (((char*)op - dest) + lastRun + 1 + ((lastRun+255-RUN_MASK_LZ4)/255) > (U32)maxOutputSize)) return 0;  /* Check output limit */
        if (lastRun>=(int)RUN_MASK_LZ4) { *op++=(RUN_MASK_LZ4<<ML_BITS_LZ4); lastRun-=RUN_MASK_LZ4; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; }
        else *op++ = (BYTE)(lastRun<<ML_BITS_LZ4);
        memcpy(op, anchor, iend - anchor);
        op += iend-anchor;
    }

    /* End */
    return (int) (((char*)op)-dest);
}
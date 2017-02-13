/*
 * Fast version of the longest_match function for zlib.
 * Copyright (C) 2004-2017 Konstantin Nosov
 * For details and updates please visit
 * http://github.com/gildor2/fast_zlib
 */

//#define PARANOID_CHECK                        /* enable to immediately validate results */
//#define REFINE_MATCHES                        /* improves compression for data which is compressed well */

#ifdef PARANOID_CHECK

#include <stdio.h>

#undef Assert
#define Assert(x, msg) { if (!(x)) { printf("Assertion: %s: %s %d: %s\n", msg, __FILE__, __LINE__, #x); exit(1); } }

#endif /* PARANOID_CHECK */

local uInt longest_match(s, cur_match)
    deflate_state *s;
    IPos cur_match;                             /* current match */
{
    unsigned chain_length = s->max_chain_length;/* max hash chain length */
    register Bytef *scan = s->window + s->strstart; /* current string */
    register Bytef *match;                      /* matched string */
    register int len;                           /* length of current match */
    int best_len = s->prev_length;              /* ignore strings, shorter or of the same length; rename ?? */
    int real_len = best_len;                    /* length of longest found match */
    int nice_match = s->nice_match;             /* stop if match long enough */
    int offset = 0;                             /* offset of current hash chain */
    IPos limit_base = s->strstart > (IPos)MAX_DIST(s) ?
        s->strstart - (IPos)MAX_DIST(s) : NIL;
    /*?? are MAX_DIST matches allowed ?! */
    IPos limit = limit_base;                    /* limit will be limit_base+offset */
    /* Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0.
     */
    Bytef *match_base = s->window;              /* s->window - offset */
    Bytef *match_base2;                         /* s->window + best_len-1 - offset */
    IPos threshold_pos = best_len > MIN_MATCH ? s->prev_match : 0;
    /* "offset search" mode will speedup only with large chain_length; plus it is
     * impossible for deflate_fast(), because this function does not perform
     * INSERT_STRING() for matched strings (hash table have "holes"). deflate_fast()'s
     * max_chain is <= 32, deflate_slow() max_chain > 64 starting from compression
     * level 6; so - offs0_mode==true only for deflate_slow() with level >= 6)
     */
    int offs0_mode = chain_length < 64;         /* bool, mode with offset==0 */
    Posf *prev = s->prev;                       /* lists of the hash chains */
    uInt wmask = s->w_mask;
#ifdef PARANOID_CHECK
    int match_found = 0;
#endif

    register Bytef *strend = s->window + s->strstart + MAX_MATCH-1;
        /* points to last byte for maximal-length scan */
    register ush scan_start = *(ushf*)scan;     /* 1st 2 bytes of scan */
    typedef ulg FAR ulgf;                       /*?? should be declared in zutil.h */
    ulg scan_start_l = *(ulgf*)scan;            /* 1st 4 bytes of scan */
    register ush scan_end;                      /* last byte of scan + next one */

#define UPDATE_MATCH_BASE2  match_base2 = match_base+best_len-1
#define UPDATE_SCAN_END     scan_end = *(ushf*)(scan+best_len-1)

    UPDATE_MATCH_BASE2;
    UPDATE_SCAN_END;

    /* The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
     * It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8, "Code too clever");

    /* Do not waste too much time if we already have a good match: */
    if (s->prev_length >= s->good_match) {
        chain_length >>= 2;
    }
    /* Do not look for matches beyond the end of the input. This is necessary
     * to make deflate deterministic.
     */
    if ((uInt)nice_match > s->lookahead) nice_match = s->lookahead;
    Assert((ulg)s->strstart <= s->window_size-MIN_LOOKAHEAD, "need lookahead");

    if (best_len > MIN_MATCH) {
        /* We're continuing search (lazy evaluation).
         * Note: for deflate_fast best_len is always MIN_MATCH-1 here
         */
        register int i;
        IPos pos;
        register uInt hash = 0;
#if (MIN_MATCH != 3) || (MAX_MATCH != 258)
#error The code is designed for MIN_MATCH==3 && MAX_MATCH==258
#endif
        /* Find a most distant chain starting from scan with index=1 (index=0 corresponds
         * to cur_match). Note: we cannot use s->prev[strstart+1,...] immediately, because
         * these strings are not yet inserted into hash table yet.
         */
        UPDATE_HASH(s, hash, scan[1]);
        UPDATE_HASH(s, hash, scan[2]);
        for (i = 3; i <= best_len; i++) {
            UPDATE_HASH(s, hash, scan[i]);
            /* If we're starting with best_len >= 3, we can use offset search. */
            pos = s->head[hash];
            if (pos < cur_match) {
                offset = i - 2;
                cur_match = pos;
            }
        }
        /* update variables to correspond offset */
        limit = limit_base + offset;
        if (cur_match <= limit) goto break_matching;
        match_base -= offset;
        match_base2 -= offset;
    }

#define NEXT_CHAIN \
    cur_match = prev[cur_match & wmask]; \
    if (cur_match <= limit) goto break_matching; \
    if (--chain_length == 0) goto break_matching; \
    Assert(cur_match - offset < s->strstart, "no future");

    do {
        /* Find a candidate for matching using hash table. Jump over hash
         * table chain until we'll have a partial march. Doing "break" when
         * matched, and NEXT_CHAIN to try different place.
         */
        if (best_len < MIN_MATCH) {
            /* Here we have best_len < MIN_MATCH, and this means, that
             * offset == 0. So, we need to check only first 2 bytes of
             * match (remaining 1 byte will be the same, because of nature of
             * hash function)
             */
            for (;;) {
                if (*(ushf*)(match_base + cur_match) == scan_start) break;
                NEXT_CHAIN;
            }
        } else if (best_len > MIN_MATCH) {
            /* current len > MIN_MATCH (>= 4 bytes); compare 1st 4 bytes and last 2 bytes */
            for (;;) {
                if (*(ushf*)(match_base2 + cur_match) == scan_end &&
                    *(ulgf*)(match_base + cur_match) == scan_start_l) break;
                NEXT_CHAIN;
            }
        } else {
            /* current len is exactly MIN_MATCH (3 bytes); compare 4 bytes */
            for (;;) {
                if (*(ulgf*)(match_base + cur_match) == scan_start_l) break;
                NEXT_CHAIN;
            }
        }

        /* Skip 1 byte */
        match = match_base + cur_match + 1;
        scan++;

        /* Found a match candidate. Compare strings to determine its length. */
#if 1
        do {
        } while (*(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
                 scan < strend);
        /* The funny "do {}" generates better code on most compilers */
#else
        //!! TODO: 64-bit version
        //!! TODO: separate loops for small best_len and large best_len
        while (scan < strend) {
            if (*(ulgf*)scan != *(ulgf*)match) break;
            scan += 4; match += 4;
            if (*(ulgf*)scan != *(ulgf*)match) break;
            scan += 4; match += 4;
//            if (*(ulgf*)scan != *(ulgf*)match) break;
//            scan += 4; match += 4;
//            if (*(ulgf*)scan != *(ulgf*)match) break;
//            scan += 4; match += 4;
        }

        if (*(ushf*)scan == *(ushf*)match)
        {
            scan += 2;
            match += 2;
        }
#endif

        /* Here, scan <= window+strstart+257 */
        Assert(scan <= s->window+(unsigned)(s->window_size-1), "wild scan");
        if (*scan == *match) scan++;

        len = (MAX_MATCH - 1) - (int)(strend-scan);
        scan = strend - (MAX_MATCH-1);

#ifdef REFINE_MATCHES
        /* Reject matches, smaller than prev_length+2 for matches, older
         * than threshold_pos (slightly improves compression ratio).
         */
        if (cur_match <= threshold_pos) {
            /* current match is more distant than prev_match */
            threshold_pos = 0;  /* disable condition above */
            if (best_len < s->prev_length + 1) {
                best_len = s->prev_length + 1;
                UPDATE_MATCH_BASE2;
                UPDATE_SCAN_END;
            }
        }
#endif /* REFINE_MATCHES */

        if (len > best_len) {
#ifdef PARANOID_CHECK
            match_found = 1;
#endif
            /* new string is longer than previous - remember it */
            s->match_start = cur_match - offset;
            real_len = best_len = len;
            if (len >= nice_match) break;
            UPDATE_SCAN_END;
            /* look for better string offset */
            if (len > MIN_MATCH && cur_match + len < s->strstart && !offs0_mode) {
                /* NOTE: if deflate algorithm will perform INSERT_STRING for
                 *   a whole scan (not for scan[0] only), can remove
                 *   "cur_match + len < s->strstart" limitation and replace it
                 *   with "cur_match + len < strend".
                 */
                IPos    pos, next_pos;
                register int i;

                /* go back to offset 0 */
                cur_match -= offset;
                offset = 0;
                next_pos = cur_match;
                for (i = 0; i <= len - MIN_MATCH; i++) {
                    pos = prev[(cur_match + i) & wmask];
                    if (pos < next_pos) {
                        /* this hash chain is more distant, use it */
                        if (pos <= limit_base + i) goto break_matching;
                        next_pos = pos;
                        offset = i;
                    }
                }
                /* switch cur_match to next_pos chain */
                cur_match = next_pos;
                /* update offset-dependent vars */
                limit = limit_base + offset;
                match_base = s->window - offset;
                UPDATE_MATCH_BASE2;
                continue;
            } else {
                /* no way to change offset - simply update match_base2 for
                 * new best_len (this is similar to what original algorithm does)
                 */
                UPDATE_MATCH_BASE2;
            }
        }
        /* follow hash chain */
        cur_match = prev[cur_match & wmask];
    } while (cur_match > limit && --chain_length != 0);

break_matching: /* sorry for goto's, but such code is smaller and easier to view ... */
#ifdef PARANOID_CHECK
    if (match_found) {
        static int warned = 0;
        int error = 0;
        if (!warned) {
            warned = 1;
            printf("WARNING: compiled with PARANOID_CHECK\n");
        }
        if (real_len > s->lookahead) real_len = s->lookahead;
        if (real_len > MAX_MATCH) {
            error = 1;
            printf("match too long\n");
        }
        if (s->match_start < limit_base) {
            error = 1;
            printf("too far $%X -> $%X [%d]  (dist+=%X)\n", s->strstart, s->match_start, real_len, limit_base-s->match_start);
        }
        if (zmemcmp(s->window + s->strstart, s->window + s->match_start, real_len)) {
            error = 1;
            printf("invalid match $%X -- $%X [%d]\n", s->strstart, s->match_start, real_len);
        }
        if (error) {
            FILE *f = fopen("match.dmp", "wb");
            if (f) {
                fwrite(s->window, s->strstart + s->lookahead, 1, f);
                fclose(f);
            } else
                printf("cannot dump");
            Assert(0, "aborted");
            exit(1);
        }
    }
#endif /* PARANOID_CHECK */
    if ((uInt)real_len <= s->lookahead) return (uInt)real_len;
    return s->lookahead;
}

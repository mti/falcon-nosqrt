#include "FalconKey.h"
#include <iostream>

static inline fpr *
align_fpr(void *tmp)
{
        uint8_t *atmp;
        unsigned off;

        atmp = (uint8_t*) tmp;
        off = (uintptr_t)atmp & 7u;
        if (off != 0) {
                atmp += 8u - off;
        }
        return (fpr *)atmp;
}

#include "vrfy_mq.h"

#define MAX(a,b) ({ \
    size_t _a = (a); \
    size_t _b = (b); \
    _a > _b ? _a : _b; \
    })

#define TMPSIZE	 MAX(FALCON_TMPSIZE_KEYGEN(GEN_LOGN), \
		     MAX(FALCON_TMPSIZE_MAKEPUB(GEN_LOGN), \
			 MAX(FALCON_TMPSIZE_SIGNDYN(GEN_LOGN), \
			     MAX(FALCON_TMPSIZE_SIGNTREE(GEN_LOGN), \
				 MAX(FALCON_TMPSIZE_EXPANDPRIV(GEN_LOGN), \
				     FALCON_TMPSIZE_VERIFY(GEN_LOGN))))))

void
FalconKey::injectfault(double faultyval) {
    fpr *tree = align_fpr(expkey + 1) + 4*GEN_SIZE + 2*(GEN_SIZE - 1);
    std::cerr << "Inserting leaf fault: " << (*tree).v << " -> " << faultyval << std::endl;
    (*tree).v = faultyval;
}

FalconKey::FalconKey() {
    Prev = bitrev_permutation(GEN_LOGN);
}

void
FalconKey::keygen() {
    uint8_t tmp[TMPSIZE];
    ptrdiff_t u = 1;

    shake256_context rng;
    shake256_init_prng_from_system(&rng);
    falcon_keygen_make(&rng, GEN_LOGN,
	    privkey, sizeof privkey,
	    pubkey,  sizeof pubkey,
	    tmp,     sizeof tmp);
    falcon_expand_privkey(expkey, sizeof expkey,
	    privkey, sizeof privkey, tmp, sizeof tmp);

    f = (int8_t*) tmp;
    g = f + GEN_SIZE;
    F = g + GEN_SIZE;
    G = F + GEN_SIZE;

    u += Zf(trim_i8_decode)(f, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(g, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(F, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(G, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);

    f = (int8_t*) tmp;
    g = f + GEN_SIZE;
    F = g + GEN_SIZE;
    G = F + GEN_SIZE;

    u += Zf(trim_i8_decode)(f, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(g, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(F, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);
    u += Zf(trim_i8_decode)(G, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
	    privkey + u, sizeof privkey - u);

    b0 = Eigen::VectorXd::Zero(2*GEN_SIZE);
    b1 = Eigen::VectorXd::Zero(2*GEN_SIZE);
    for(u = 0; u < GEN_SIZE; u++) {
        b0[u]          =  (double) g[u];
        b0[u+GEN_SIZE] = -(double) f[u];
        b1[u]          =  (double) G[u];
        b1[u+GEN_SIZE] = -(double) F[u];
    }

    basis = Eigen::MatrixXd::Zero(2*GEN_SIZE, 2*GEN_SIZE);
    for(u = 0; u < GEN_SIZE; u++) {
        for(ptrdiff_t v = 0; v < GEN_SIZE; v++) {
            basis(u,v)          = (u>=v) ? b0[u-v] : (-b0[GEN_SIZE+u-v]);
            basis(u,v+GEN_SIZE) = (u>=v) ? b1[u-v] : (-b1[GEN_SIZE+u-v]);

            basis(u+GEN_SIZE,v)          = (u>=v) ? b0[GEN_SIZE+u-v] : (-b0[2*GEN_SIZE+u-v]);
            basis(u+GEN_SIZE,v+GEN_SIZE) = (u>=v) ? b1[GEN_SIZE+u-v] : (-b1[2*GEN_SIZE+u-v]);
        }
    }
    basis = Prev * basis * Prev;

    Eigen::HouseholderQR<Eigen::MatrixXd> qrbasis(basis);
    gso = qrbasis.householderQ();
    gsnorms = qrbasis.matrixQR().diagonal();

    for (u = 0; u < gsnorms.size(); u++) {
        if(gsnorms[u] < 0) {
            gsnorms[u] *= -1;   // Flip the sign of row i in R
            gso.col(u) *= -1;   // Flip the sign of column i in Q to keep Q*R unchanged
        }
    }
}

void
FalconKey::load(char* path) {
    uint8_t tmp[TMPSIZE];
    char    fnamebuf[1024];
    FILE*   file;
    ptrdiff_t u = 1;

    shake256_context rng;
    shake256_init_prng_from_system(&rng);

    snprintf(fnamebuf, sizeof fnamebuf, "%s/privkey", path);
    file = fopen(fnamebuf, "wbx");
    if(file == NULL) {
	fprintf(stderr, "Private key file %s exists: "
			"reading privkey.\n", fnamebuf);
	file = fopen(fnamebuf, "rb");
	size_t len = fread(privkey, 1, sizeof privkey, file);
	if(len < sizeof privkey) {
	    fprintf(stderr, "%s: private key file %s too short.\n",
		    "FalconKey", fnamebuf);
	    fclose(file);
	    exit(1);
	}
	falcon_make_public(pubkey, sizeof pubkey,
		privkey, sizeof privkey, tmp, sizeof tmp);
    } else {
    fprintf(stderr, "Create new key in private key file %s.\n", fnamebuf);

	falcon_keygen_make(&rng, GEN_LOGN,
		privkey, sizeof privkey,
		pubkey,  sizeof pubkey,
		tmp,     sizeof tmp);
	size_t len = fwrite(privkey, 1, sizeof privkey, file);
	if(len < sizeof privkey) {
	    fprintf(stderr, "%s: could not fully write private key file %s.\n",
		    "FalconKey", fnamebuf);
	    fclose(file);
	    exit(1);
	}
    fclose(file);
    }

    falcon_expand_privkey(expkey, sizeof expkey,
	    privkey, sizeof privkey, tmp, sizeof tmp);

        f = (int8_t*) tmp;
        g = f + GEN_SIZE;
        F = g + GEN_SIZE;
        G = F + GEN_SIZE;
    
        u += Zf(trim_i8_decode)(f, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(g, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(F, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(G, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
    
        f = (int8_t*) tmp;
        g = f + GEN_SIZE;
        F = g + GEN_SIZE;
        G = F + GEN_SIZE;
    
        u += Zf(trim_i8_decode)(f, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(g, GEN_LOGN, Zf(max_fg_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(F, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
        u += Zf(trim_i8_decode)(G, GEN_LOGN, Zf(max_FG_bits)[GEN_LOGN],
            privkey + u, sizeof privkey - u);
    
        b0 = Eigen::VectorXd::Zero(2*GEN_SIZE);
        b1 = Eigen::VectorXd::Zero(2*GEN_SIZE);
        for(u = 0; u < GEN_SIZE; u++) {
            b0[u]          =  (double) g[u];
            b0[u+GEN_SIZE] = -(double) f[u];
            b1[u]          =  (double) G[u];
            b1[u+GEN_SIZE] = -(double) F[u];
        }
    
        basis = Eigen::MatrixXd::Zero(2*GEN_SIZE, 2*GEN_SIZE);
        for(u = 0; u < GEN_SIZE; u++) {
            for(ptrdiff_t v = 0; v < GEN_SIZE; v++) {
                basis(u,v)          = (u>=v) ? b0[u-v] : (-b0[GEN_SIZE+u-v]);
                basis(u,v+GEN_SIZE) = (u>=v) ? b1[u-v] : (-b1[GEN_SIZE+u-v]);
    
                basis(u+GEN_SIZE,v)          = (u>=v) ? b0[GEN_SIZE+u-v] : (-b0[2*GEN_SIZE+u-v]);
                basis(u+GEN_SIZE,v+GEN_SIZE) = (u>=v) ? b1[GEN_SIZE+u-v] : (-b1[2*GEN_SIZE+u-v]);
            }
        }
        basis = Prev * basis * Prev;
    
        Eigen::HouseholderQR<Eigen::MatrixXd> qrbasis(basis);
        gso = qrbasis.householderQ();
        gsnorms = qrbasis.matrixQR().diagonal();
    
        for (u = 0; u < gsnorms.size(); u++) {
            if(gsnorms[u] < 0) {
                gsnorms[u] *= -1;   // Flip the sign of row i in R
                gso.col(u) *= -1;   // Flip the sign of column i in Q to keep Q*R unchanged
            }
        }
}

// Function to reverse the bits of an index
unsigned int
FalconKey::bitrev(unsigned int x, int n) {
    unsigned int r = 0;
    for (int i = 0; i < n; ++i) {
        if (x & (1 << i)) {
            r |= 1 << (n - 1 - i);
        }
    }
    return r;
}

Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic>
FalconKey::bitrev_permutation(int logn) {
    int n = 1 << logn;
    Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic> P(2*n);

    for (int i = 0; i < n; i++) {
        P.indices()[i  ] = bitrev(i, logn);
        P.indices()[i+n] = bitrev(i, logn) + n;
    }

    return P;
}

Eigen::MatrixXd 
FalconKey::gen_sigmatrix(size_t nsigs) const
{
    uint8_t  tmp[TMPSIZE];
    uint8_t  msg[50];

    Eigen::MatrixXd sigmatrix(2*GEN_SIZE, nsigs);

    size_t   n = GEN_SIZE;
    uint16_t *h, *hm, *tt2;
    int16_t  *sig;
    uint8_t  *tt;

    fpr *expanded_key = align_fpr((uint8_t *)expkey + 1);

    h = (uint16_t *)tmp;
    hm = h + n;
    sig = (int16_t *)(hm + n);
    tt = (uint8_t *)(sig + n);

    Zf(modq_decode)(h, GEN_LOGN, ((uint8_t*)pubkey) + 1, (sizeof pubkey) - 1);
    Zf(to_ntt_monty)(h, GEN_LOGN);

    shake256_context rng;
    shake256_init_prng_from_system(&rng);

    for(size_t i = 0; i < nsigs; i++) {
        inner_shake256_context sc;

        shake256_extract(&rng, msg, sizeof msg);
        inner_shake256_init(&sc);
        inner_shake256_inject(&sc, msg, sizeof msg);
        inner_shake256_flip(&sc);
        Zf(hash_to_point_vartime)(&sc, hm, GEN_LOGN);
        Zf(sign_tree)(sig, (inner_shake256_context*)&rng,
                expanded_key, hm, GEN_LOGN, tt);

        size_t u;
        tt2 = (uint16_t*)tt;
        for(u = 0; u < n; u++) {
            uint32_t w;

            w = (uint32_t)sig[u];
            w += Q & -(w >> 31);
            tt2[u] = (uint16_t)w;
        }

        mq_NTT(tt2, GEN_LOGN);
        mq_poly_montymul_ntt(tt2, h, GEN_LOGN);
        mq_iNTT(tt2, GEN_LOGN);
        mq_poly_sub(tt2, hm, GEN_LOGN);

        for(u = 0; u < n; u++) {
            int32_t w;

            w = (int32_t)tt2[u];
            w-= (int32_t)(Q & -(((Q >> 1) - (uint32_t)w) >> 31));
            ((int16_t *)tt2)[u] = (int16_t)w;
        }
        
        for(u = 0; u < n; u++) {
            sigmatrix(u,   i) = -(double) ((int16_t*)tt2)[u];
            sigmatrix(u+n, i) =  (double) sig[u];
        }
    }
    return Prev * sigmatrix;
}


#pragma once

#include <random>

#include "util/common.h"

namespace tpl::util {

/**
 * Fast-random number generater based on Lehmer's technique.
 *
 * D. H. Lehmer, Mathematical methods in large-scale computing units.
 * Proceedings of a Second Symposium on Large Scale Digital Calculating
 * Machinery;
 * Annals of the Computation Laboratory, Harvard Univ. 26 (1951), pp. 141-146.
 *
 * P L'Ecuyer,  Tables of linear congruential generators of different sizes and
 * good lattice structure. Mathematics of Computation of the American
 * Mathematical
 * Society 68.225 (1999): 249-260.
 */
class FastRand {
 public:
  FastRand() {
    // Seed this fast random number generator with a number generated using a
    // slower PRNG.
    std::random_device rand_dev;
    std::mt19937_64 mt(rand_dev());
    std::uniform_int_distribution<u64> dist;
    state_ = dist(mt);
  }

  u64 Next() {
    state_ *= 0xda942042e4dd58b5ull;
    return state_ >> 64u;
  }

 private:
  u128 state_;
};

}  // namespace tpl::util
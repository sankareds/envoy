#pragma once

#include <initializer_list>
#include <type_traits>
#include <utility>

namespace Envoy {

template <class I, class M> struct Phantom;

// Minor optimization: keeping these as free functions avoids duplicating them for every
// Phantom type, at the cost of making the globally accessible.
namespace Internal {
// Helper structs to determine whether the base inner type is constructible from a
// std::initializer_list<S>. Exist as structs instead of constexpr funcitons to allow for template
// specialization.
//
// These are needed because whether to include the std::initializer ctor depends on whether the
// innermost type has a applicable ctor, and for some reason inspecing just the immediate inner type
// is not sufficient.
template <class I, class S> struct constructibleFromList {
  static constexpr bool value = std::is_constructible<I, std::initializer_list<S>>::value;
};

template <class I, class M, class S> struct constructibleFromList<Phantom<I, M>, S> {
  static constexpr bool value = constructibleFromList<I, S>::value;
};
} // namespace Internal

/**
 * A phantom type provides additional type safety to types that are otherwise interchangeable.
 * For instance, two std::vector<uint32_t> might have different semantic meaning, so expressing
 * them with a phantom type provides some compile time guarantees that they won't be used
 * interchangeably.
 *
 * Since a phantom type subclasses the inner type, they can be used wherever the inner type is
 * required.
 *
 * This template currently only works for non-primitive types due to its reliance on subclassing.
 */
template <class I, class M> struct Phantom : I {
  template <class... Args> static Phantom<I, M> create(Args&&... args) {
    return Phantom<I, M>(std::forward<Args>(args)...);
  }

  // To allow inner types to be constructed from initializer lists, we add a function specifically
  // for std::initializer_list. This allows invokations such as Phantom<Foo, Bar> f{1,2,3} to prefer
  // std::initializer_list constructors if one exists.
  //
  // We use std::enable_if to ensure that this function is omitted when the inner type does not
  // have a std::initializer ctor.
  template <class S, std::enable_if_t<Internal::constructibleFromList<I, S>::value, int> = 0>
  static Phantom<I, M> create(std::initializer_list<S> init) {
    return Phantom<I, M>(init);
  }

  // The default constructor doesn't have any issues with variadic template type inferrence,
  // so allow it directly if the inner type allows it.
  // TODO(snowp): If we ever want to support inner types without a default ctor, we'll
  // need to template specialize this entire class as SFINAE can't be used with a no-arg function.
  Phantom() : I() {}

protected:
  // This allows invoking any of the ctors of the inner class. Since this has consequences for how
  // the initializer list works with overload resolution, we restrict access to this ctor to avoid
  // confusion.
  //
  // For posterity's sake: the issue is that the initializer list is not forwarded even with perfect
  // forwarding. The initializer list is inferred to a set of parameter types before forwarding,
  // and there seems to be a difference in how the generic varags ctor is inferred vs the regular
  // ctor. For example:
  //
  // This resolves to int&&, int&&, invokes size_t, const uint32_t& ctor:
  // Phantom<std::vector<uint32_t>> p{1,2};
  //
  // This resolves to std::initializer_list<int, int>, invokes initalizer ctor:
  // std::vector<uint32_t> v{1,2};
  //
  // This resolves to std::initializer_list<unsigned int, unsigned int>, invokes initalizer ctor:
  // Phantom<std::vector<uint32_t>> p{1u,2u};
  //
  // To ensure that the phantom type is not implictly created from the ctors of the inner type,
  // these ctors are marked explicit.
  template <class... S> explicit Phantom(S&&... v) : I(std::forward<S>(v)...) {}
};

} // namespace Envoy

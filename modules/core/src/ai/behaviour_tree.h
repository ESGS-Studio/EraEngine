// Copyright (c) 2023-present Eldar Muradov. All rights reserved.

#pragma once

#include "core_api.h"

#include <aitoolkit/behtree.hpp>

namespace era_engine::ai
{
    template <typename Type>
    struct BehaviourTree
    {
        BehaviourTree()
        {
            tree = aitoolkit::bt::sel<Type>::make(
                {
                    aitoolkit::bt::seq<Type>::make(
                    {
                        aitoolkit::bt::check<Type>::make([](const Type& bb)
                        {
                            return true;
                        }),
                        aitoolkit::bt::task<Type>::make([](Type& bb)
                        {
                            return aitoolkit::bt::execution_state::success;
                        })
                    })
                });
        }

        aitoolkit::bt::execution_state evaluate(Type type);

        aitoolkit::bt::sel<Type> tree;
    };

    template<typename Type>
    inline aitoolkit::bt::execution_state BehaviourTree<Type>::evaluate(Type type)
    {
        auto state = tree.evaluate(type);
        return state;
    }
}
#pragma once

#include "SequenceDefinition.h"

namespace sequence
{
    // Reconnect incoming links to the deleted node's successor. A jump to a
    // deleted terminal node becomes end_sequence; deleting the last node leaves
    // an empty draft with no start node.
    bool DeleteNode(SequenceDefinition& definition, const std::string& id);
}

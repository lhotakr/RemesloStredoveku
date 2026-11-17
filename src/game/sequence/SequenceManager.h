#pragma once

#include "SequenceDefinition.h"
#include "SequenceRuntime.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sequence
{
    class SequenceManager
    {
    public:
        bool loadDirectory(const std::filesystem::path& directory);
        bool reload();

        bool start(const std::string& sequenceId, ISequenceContext& context);
        bool startDefinition(const SequenceDefinition& definition, ISequenceContext& context, const std::string& nodeId = {});
        bool startAtNode(const std::string& sequenceId, const std::string& nodeId, ISequenceContext& context);
        void stop();
        void update(float dt);
        void stepAction();
        bool skip();
        void setPaused(bool paused) { m_runtime.setPaused(paused); }

        bool isRunning() const { return m_runtime.isRunning(); }
        Status status() const { return m_runtime.status(); }

        const SequenceDefinition* find(const std::string& sequenceId) const;
        const std::vector<std::string>& ids() const { return m_ids; }
        const std::vector<std::string>& errors() const { return m_errors; }
        const std::filesystem::path& directory() const { return m_directory; }

    private:
        std::filesystem::path m_directory;
        std::unordered_map<std::string, SequenceDefinition> m_sequences;
        std::vector<std::string> m_ids;
        std::vector<std::string> m_errors;
        SequenceRuntime m_runtime;
    };
}

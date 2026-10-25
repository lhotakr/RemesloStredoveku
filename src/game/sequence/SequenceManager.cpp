#include "SequenceManager.h"

#include <algorithm>

namespace sequence
{
    bool SequenceManager::loadDirectory(const std::filesystem::path& directory)
    {
        m_directory = directory;
        return reload();
    }

    bool SequenceManager::reload()
    {
        stop();
        m_sequences.clear();
        m_ids.clear();
        m_errors.clear();

        std::error_code ec;
        std::filesystem::create_directories(m_directory, ec);
        if (ec)
        {
            m_errors.push_back("Cannot create sequence directory: " + m_directory.string() + " | " + ec.message());
            return false;
        }

        for (const auto& entry : std::filesystem::directory_iterator(m_directory, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".json")
                continue;

            LoadResult loaded = LoadSequenceFromFile(entry.path().string());
            for (const auto& e : loaded.errors)
                m_errors.push_back(e);
            for (const auto& w : loaded.warnings)
                m_errors.push_back("Warning: " + w);

            if (!loaded.ok)
                continue;

            const std::string id = loaded.sequence.id;
            if (m_sequences.find(id) != m_sequences.end())
            {
                m_errors.push_back("Duplicate sequence id '" + id + "' in " + entry.path().string());
                continue;
            }

            m_sequences.emplace(id, std::move(loaded.sequence));
            m_ids.push_back(id);
        }

        std::sort(m_ids.begin(), m_ids.end());
        return m_errors.empty();
    }

    bool SequenceManager::start(const std::string& sequenceId, ISequenceContext& context)
    {
        const SequenceDefinition* def = find(sequenceId);
        if (!def)
        {
            context.sequenceLog("Sequence not found: " + sequenceId);
            return false;
        }
        return m_runtime.start(def, &context);
    }

    bool SequenceManager::startDefinition(const SequenceDefinition& definition, ISequenceContext& context, const std::string& nodeId)
    {
        // Own a snapshot so editing the source definition cannot invalidate a running preview.
        SequenceDefinition snapshot = definition;
        stop();
        const std::string id = snapshot.id;
        m_sequences.insert_or_assign(id, std::move(snapshot));
        if (std::find(m_ids.begin(), m_ids.end(), id) == m_ids.end())
        {
            m_ids.push_back(id);
            std::sort(m_ids.begin(), m_ids.end());
        }
        return startAtNode(id, nodeId, context);
    }

    bool SequenceManager::startAtNode(const std::string& sequenceId, const std::string& nodeId, ISequenceContext& context)
    {
        const SequenceDefinition* def = find(sequenceId);
        if (!def)
        {
            context.sequenceLog("Sequence not found: " + sequenceId);
            return false;
        }
        return m_runtime.startAtNode(def, &context, nodeId);
    }

    void SequenceManager::stop()
    {
        m_runtime.stop();
    }

    void SequenceManager::update(float dt)
    {
        m_runtime.update(dt);
    }

    void SequenceManager::stepAction()
    {
        m_runtime.stepAction();
    }

    bool SequenceManager::skip()
    {
        return m_runtime.skip();
    }

    const SequenceDefinition* SequenceManager::find(const std::string& sequenceId) const
    {
        const auto it = m_sequences.find(sequenceId);
        if (it == m_sequences.end())
            return nullptr;
        return &it->second;
    }
}

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

    const SequenceDefinition* SequenceManager::find(const std::string& sequenceId) const
    {
        const auto it = m_sequences.find(sequenceId);
        if (it == m_sequences.end())
            return nullptr;
        return &it->second;
    }
}

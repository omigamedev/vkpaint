#pragma once

using cs = vk::ComponentSwizzle;
using cc = vk::ColorComponentFlagBits;

struct vertex_t
{
    glm::vec3 pos;
    glm::vec3 col;
    glm::vec2 tex;
    vertex_t() = default;
    constexpr vertex_t(glm::vec3 p, glm::vec3 c, glm::vec2 t) : pos(p), col(c), tex(t) {}
};

int find_memory(const vk::PhysicalDevice& pd, const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
std::vector<uint8_t> read_file(const std::filesystem::path& path);
vk::UniqueShaderModule load_shader(const vk::UniqueDevice& dev, const std::filesystem::path& path);

std::tuple<vk::UniqueImage, vk::UniqueImageView, vk::UniqueDeviceMemory> 
    create_depth(const vk::PhysicalDevice& pd, vk::Device const& dev, int width, int height);
vk::UniqueSampler create_sampler(const vk::UniqueDevice& dev);
auto create_triangle(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev);

template<typename T>
class UBO
{
public:
    vk::UniqueBuffer m_buffer;
    vk::UniqueDeviceMemory m_memory;
    T m_value;
    bool create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
    {
        auto info = vk::BufferCreateInfo({}, sizeof(T), vk::BufferUsageFlagBits::eUniformBuffer);
        m_buffer = dev->createBufferUnique(info);
        auto mem_req = dev->getBufferMemoryRequirements(*m_buffer);
        auto mem_idx = find_memory(pd, mem_req,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        m_memory = dev->allocateMemoryUnique({ mem_req.size, (uint32_t)mem_idx });
        dev->bindBufferMemory(*m_buffer, *m_memory, 0);
        return true;
    }
    void update(const vk::UniqueDevice& dev)
    {
        if (auto uniform_map = static_cast<T*>(dev->mapMemory(*m_memory, 0, VK_WHOLE_SIZE)))
        {
            std::copy_n(&m_value, 1, uniform_map);
            dev->unmapMemory(*m_memory);
        }
    }
    static UBO<T> create_static(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
    {
        UBO<T> ubo;
        if (!ubo.create(pd, dev))
            throw std::runtime_error("UBO creation failed");
        return ubo;
    }
};

inline void* aligned_malloc(size_t size, size_t align) {
    void* result;
#ifdef _MSC_VER 
    result = _aligned_malloc(size, align);
#else 
    if (posix_memalign(&result, align, size)) result = 0;
#endif
    return result;
}

inline void aligned_free(void* ptr) {
#ifdef _MSC_VER 
    _aligned_free(ptr);
#else 
    free(ptr);
#endif

}

// used as: std::vector<T, AlignmentAllocator<T, 16> > bla;
template <typename T, std::size_t N = 16>
class AlignmentAllocator {
public:
    typedef T value_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    typedef T* pointer;
    typedef const T* const_pointer;

    typedef T& reference;
    typedef const T& const_reference;

public:
    inline AlignmentAllocator() throw () { }

    template <typename T2>
    inline AlignmentAllocator(const AlignmentAllocator<T2, N>&) throw () { }

    inline ~AlignmentAllocator() throw () { }

    inline pointer adress(reference r) {
        return &r;
    }

    inline const_pointer adress(const_reference r) const {
        return &r;
    }

    inline pointer allocate(size_type n) {
        return (pointer)aligned_malloc(n * sizeof(value_type), N);
    }

    inline void deallocate(pointer p, size_type) {
        aligned_free(p);
    }

    inline void construct(pointer p, const value_type& wert) {
        new (p) value_type(wert);
    }

    inline void destroy(pointer p) {
        p->~value_type();
    }

    inline size_type max_size() const throw () {
        return size_type(-1) / sizeof(value_type);
    }

    template <typename T2>
    struct rebind {
        typedef AlignmentAllocator<T2, N> other;
    };

    bool operator!=(const AlignmentAllocator<T, N>& other) const {
        return !(*this == other);
    }

    // Returns true if and only if storage allocated from *this
    // can be deallocated from other, and vice versa.
    // Always returns true for stateless allocators.
    bool operator==(const AlignmentAllocator<T, N>& other) const {
        return true;
    }
};


template<typename T> struct cbuffer
{
    std::unique_ptr<T[]> m_vec;
    int m_capacity = 0;
    int m_count = 0;
    int m_index = 0;
    cbuffer(int initial_capacity)
    {
        m_capacity = initial_capacity;
        m_index = 0;
        m_count = 0;
        m_vec = std::make_unique<T[]>(m_capacity);
    }
    void resize(int new_capacity)
    {
        m_capacity = new_capacity;
        m_vec = std::make_unique<T[]>(m_capacity);
        m_index = 0;
        m_count = 0;
    }
    void clear()
    {
        m_index = 0;
        m_count = 0;
    }
    const T& head() const
    {
        return m_index == 0 ? m_vec[m_count - 1] : m_vec[m_index - 1];
    }
    void add(const T& v)
    {
        m_vec[m_index] = v;
        m_index = (m_index + 1) % m_capacity;
        m_count = m_count < m_capacity ? m_count + 1 : m_count;
    }
    int count() const
    {
        return m_count;
    }
    template<typename T2 = T> T2 average() const
    {
        T2 tot{};
        if (m_count == 0)
            return tot;
        for (int i = 0; i < m_count; i++)
            tot += m_vec[i];
        return tot / (float)m_count;
    }
    template<typename T2 = T> T2 average_threshold(T threshold) const
    {
        T2 tot{};
        if (m_count == 0)
            return tot;
        int n = 0;
        for (int i = 0; i < m_count; i++)
            tot += glm::abs(m_vec[i] - head()) < threshold ? 0 : m_vec[i], n++;
        return tot / (float)n;
    }
};

template<typename T, int Max = 0>
class BlockingQueue
{
public:
    std::deque<std::pair<T, bool>> q;
    std::condition_variable post_cv;
    std::condition_variable get_cv;
    mutable std::mutex mutex;
    volatile bool unlocked = false;
    BlockingQueue() = default;
    BlockingQueue(const BlockingQueue& other) = delete;
    BlockingQueue& operator=(const BlockingQueue& other) = delete;
    BlockingQueue(BlockingQueue&& other) : q(std::move(q)) { }
    BlockingQueue& operator=(BlockingQueue&& other) { q = std::move(q); return *this; }
    void Post(T pkt)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (Max > 0)
        {
            post_cv.wait(lock, [&]() { return unlocked | (q.size() < Max); });
            if (q.size() >= Max) return;
        }
        q.push_back({ pkt, false });
        get_cv.notify_one();
    }
    void PostUnique(T pkt, bool top)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (Max > 0)
        {
            post_cv.wait(lock, [&]() { return unlocked | (q.size() < Max); });
            if (q.size() >= Max) return;
        }
        auto search = std::make_pair(pkt, top);
        if (std::find(q.begin(), q.end(), search) == q.end())
        {
            if (top)
            {
                // find the first low priority
                auto low = std::find_if(q.begin(), q.end(), [](auto const& x) { return x.second == false; });
                q.insert(low, { pkt, top });
            }
            else
            {
                q.push_back({ pkt, top });
            }
        }
        get_cv.notify_one();
    }
    void Remove(T pkt)
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = std::find_if(q.begin(), q.end(), [&pkt](auto const& x) { return x.first == pkt; });
        if (it != q.end())
            q.erase(it);
        if (Max > 0) post_cv.notify_all();
    }
    T Get()
    {
        static T emptyT{};
        std::unique_lock<std::mutex> lock(mutex);
        get_cv.wait(lock, [&]() { return unlocked | (q.size() > 0); });
        if (q.empty())
            return std::move(emptyT);
        auto tmp = std::move(q.front());
        q.pop_front();
        if (Max > 0) post_cv.notify_all();
        return std::move(tmp.first);
    }
    void UnlockGetters()
    {
        unlocked = true;
        get_cv.notify_all();
        if (Max > 0) post_cv.notify_all();
    }
    int Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return (int)q.size();
    }
};

# AeroMesh Security Audit Report

**Дата:** 11 июня 2026  
**Проект:** AeroMesh — анонимный P2P мессенджер  
**Версия:** Текущая (в разработке)

---

## Executive Summary

Проведен комплексный анализ безопасности кодовой базы AeroMesh. Обнаружено **23 критических и высоких уязвимости**, которые могут привести к деанонимизации пользователей, компрометации ключей, DoS-атакам и другим серьезным проблемам безопасности.

**Критичность:**
- 🔴 **Критические:** 8 уязвимостей
- 🟠 **Высокие:** 15 уязвимостей
- 🟡 **Средние:** множество архитектурных проблем

---

## 🔴 КРИТИЧЕСКИЕ УЯЗВИМОСТИ

### 1. Утечка анонимности через DHT (Sybil Attack)

**Файл:** `core/src/kademlia.cpp`, `client/DhtService.h`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
// kademlia.cpp:48 - нет проверки подлинности узлов
void Dht::handle_find_node(const NodeId& target) const {
    return table_.closest(target, k_);  // Возвращает узлы без проверки
}

// DhtService.h:238 - принимаем любые контакты от любых узлов
auto decoded = aeromesh::decode_contacts(rest);
if (decoded) {
    for (const auto& c : *decoded) {
        dht_->table().update(c);  // ❌ Нет проверки подлинности!
        learned.push_back(c);
    }
}
```

**Атака:**
1. Злоумышленник создает тысячи поддельных узлов (Sybil nodes)
2. Заполняет таблицу маршрутизации жертвы своими узлами
3. Перехватывает все запросы FIND_NODE и видит, кого ищет жертва
4. Деанонимизирует пользователя по паттернам поиска

**Последствия:**
- Полная деанонимизация через анализ графа социальных связей
- Атакующий видит, с кем общается пользователь
- Возможность MitM-атак через контроль маршрутизации

**Исправление:**
```cpp
// Добавить подпись узлов в DHT
struct SignedContact {
    Contact contact;
    std::array<std::byte, 64> signature;  // Ed25519 подпись
    std::uint64_t timestamp;
};

// Проверять подпись перед добавлением в таблицу
bool verify_contact(const SignedContact& sc) {
    // Подпись должна быть от NodeId, соответствующего публичному ключу
    auto transcript = make_contact_transcript(sc.contact, sc.timestamp);
    return Identity::verify(derive_pubkey(sc.contact.id), transcript, sc.signature);
}

// В update():
if (!verify_contact(signed_contact)) {
    return;  // Отклоняем неподписанные контакты
}
```

---

### 2. Timing Attack на Ratchet через Skipped Messages

**Файл:** `core/src/ratchet.cpp:141-157`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
std::expected<std::vector<std::byte>, RatchetError> Ratchet::decrypt_impl(
    const RatchetMessage& msg, std::span<const std::byte> associated_data) {
    // 1) Проверка в skipped_ - время зависит от размера map
    const auto skipped_key = std::make_pair(msg.dh_pub, msg.n);
    if (auto it = skipped_.find(skipped_key); it != skipped_.end()) {
        RatchetKey mk = it->second;
        auto pt = try_decrypt(mk, msg, associated_data);
        if (pt) {
            skipped_.erase(it);  // ❌ Разное время выполнения!
            return pt;
        }
        return std::unexpected(RatchetError::DecryptFailed);
    }
    // ... остальной код
}
```

**Атака:**
1. Злоумышленник отправляет сообщения с разными номерами `n`
2. Измеряет время расшифровки (через сетевые задержки ответов)
3. Определяет, какие ключи находятся в `skipped_` map
4. Восстанавливает информацию о пропущенных сообщениях

**Последствия:**
- Утечка метаданных о паттернах коммуникации
- Возможность определить, когда были пропущены сообщения
- Деградация forward secrecy

**Исправление:**
```cpp
std::expected<std::vector<std::byte>, RatchetError> Ratchet::decrypt_impl(
    const RatchetMessage& msg, std::span<const std::byte> associated_data) {
    // Всегда выполняем одинаковое количество операций
    bool found_in_skipped = false;
    RatchetKey candidate_mk{};
    
    const auto skipped_key = std::make_pair(msg.dh_pub, msg.n);
    auto it = skipped_.find(skipped_key);
    if (it != skipped_.end()) {
        found_in_skipped = true;
        candidate_mk = it->second;
    }
    
    // Всегда пытаемся расшифровать (constant-time)
    auto pt_skipped = try_decrypt(candidate_mk, msg, associated_data);
    
    if (found_in_skipped && pt_skipped) {
        skipped_.erase(it);
        return pt_skipped;
    }
    
    // Продолжаем обычную логику...
    // (но теперь время выполнения не зависит от наличия в skipped_)
}
```

---

### 3. Отсутствие Rate Limiting — DoS через DHT

**Файл:** `client/DhtService.h:195-210`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
void onQuery(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
    if (!dht_ || p.payload.size() < 2 * aeromesh::kIdBytes) {
        return;
    }
    const aeromesh::NodeId target = readId(p.payload, 0);
    const aeromesh::NodeId sender = readId(p.payload, aeromesh::kIdBytes);
    dht_->table().update(aeromesh::Contact{sender, from.to_string()});
    const auto results = dht_->handle_find_node(target);
    sendReply(from, target, results);  // ❌ Нет ограничения скорости!
}
```

**Атака:**
1. Злоумышленник отправляет тысячи DhtQuery пакетов
2. Каждый запрос вызывает `handle_find_node()` и `sendReply()`
3. Жертва тратит CPU на обработку и bandwidth на ответы
4. Приложение становится неотзывчивым

**Последствия:**
- DoS атака на узел
- Истощение CPU и сетевого канала
- Невозможность обрабатывать легитимные запросы

**Исправление:**
```cpp
class DhtService {
private:
    struct RateLimiter {
        std::unordered_map<std::string, std::deque<uint64_t>> requests;
        const size_t max_requests = 10;  // 10 запросов
        const uint64_t window_ms = 1000;  // за 1 секунду
        
        bool allow(const std::string& endpoint, uint64_t now_ms) {
            auto& times = requests[endpoint];
            // Удаляем старые запросы
            while (!times.empty() && now_ms - times.front() > window_ms) {
                times.pop_front();
            }
            if (times.size() >= max_requests) {
                return false;  // Превышен лимит
            }
            times.push_back(now_ms);
            return true;
        }
    };
    
    RateLimiter rate_limiter_;

public:
    void onQuery(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
        // Проверяем rate limit
        if (!rate_limiter_.allow(from.to_string(), NetworkEngine::clockMs())) {
            ++stats_.rate_limited;
            return;  // Отклоняем запрос
        }
        
        // Остальная логика...
    }
};
```

---

### 4. Утечка IP через STUN без Tor/VPN

**Файл:** `core/src/stun.cpp:115-127`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
std::expected<Endpoint, StunError> discover_reflexive_address(
    IDatagramSocket& socket, const Endpoint& stun_server, int max_polls) {
    const StunBindingRequest req = make_binding_request();
    if (!socket.send(stun_server, req.datagram)) {
        return std::unexpected(StunError::SendFailed);
    }
    // ❌ Прямое подключение к STUN серверу раскрывает реальный IP!
}
```

**Атака:**
1. Пользователь думает, что он анонимен
2. При NAT traversal приложение подключается к STUN серверу
3. STUN сервер (или наблюдатель) видит реальный IP пользователя
4. Анонимность полностью скомпрометирована

**Последствия:**
- **ПОЛНАЯ ДЕАНОНИМИЗАЦИЯ** пользователя
- Реальный IP адрес раскрывается третьей стороне
- Нарушение основного обещания приложения

**Исправление:**
```cpp
// 1. Добавить предупреждение в UI
Q_INVOKABLE QString discoverNatType() {
    if (!is_using_tor_or_vpn()) {
        return QStringLiteral("WARNING: STUN will reveal your real IP address. "
                            "Use Tor or VPN for anonymity.");
    }
    // ... продолжить
}

// 2. Добавить опцию использования Tor SOCKS5 proxy
class TorSocket : public IDatagramSocket {
    // Проксировать UDP через Tor (сложно, но возможно через UDP-over-TCP)
};

// 3. Использовать только relay, без STUN
// Relay не раскрывает IP, но медленнее
```

**ВАЖНО:** Документация должна ЯВНО предупреждать:
```markdown
⚠️ **КРИТИЧЕСКОЕ ПРЕДУПРЕЖДЕНИЕ ПО БЕЗОПАСНОСТИ**

AeroMesh НЕ ОБЕСПЕЧИВАЕТ анонимность IP-адреса по умолчанию!

Для настоящей анонимности вы ДОЛЖНЫ:
1. Использовать Tor Browser или VPN
2. Отключить STUN/hole punching
3. Использовать только relay-соединения

Без этого ваш реальный IP будет виден:
- STUN серверам
- Relay серверам  
- Вашим собеседникам (через DHT)
```

---

### 5. Отсутствие Replay Protection в Onion Routing

**Файл:** `core/src/onion.cpp:82-120`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
std::expected<Peeled, OnionError> peel_onion(
    std::span<const std::byte> onion,
    std::span<const std::byte> my_x25519_pub,
    std::span<const std::byte> my_x25519_secret) {
    // ... расшифровка слоя
    const int rc = crypto_box_seal_open(
        reinterpret_cast<unsigned char*>(opened.data()),
        reinterpret_cast<const unsigned char*>(onion.data()),
        onion.size(),
        reinterpret_cast<const unsigned char*>(my_x25519_pub.data()),
        reinterpret_cast<const unsigned char*>(my_x25519_secret.data()));
    // ❌ Нет проверки на replay! Один и тот же onion можно отправить дважды
}
```

**Атака:**
1. Злоумышленник перехватывает onion-пакет
2. Отправляет его повторно (replay attack)
3. Relay узлы обрабатывают его снова
4. Можно определить маршрут по времени обработки

**Последствия:**
- Replay атаки на onion routing
- Возможность traffic analysis через повторную отправку
- Утечка информации о маршруте

**Исправление:**
```cpp
class OnionReplayFilter {
private:
    std::unordered_set<std::string> seen_hashes_;
    std::deque<std::pair<std::string, uint64_t>> expiry_queue_;
    const uint64_t ttl_ms_ = 60000;  // 1 минута
    
public:
    bool is_replay(std::span<const std::byte> onion, uint64_t now_ms) {
        // Хеш onion пакета
        std::array<unsigned char, 32> hash;
        crypto_generichash(hash.data(), hash.size(),
                          reinterpret_cast<const unsigned char*>(onion.data()),
                          onion.size(), nullptr, 0);
        
        std::string hash_str(reinterpret_cast<char*>(hash.data()), 32);
        
        // Удаляем старые записи
        while (!expiry_queue_.empty() && 
               now_ms - expiry_queue_.front().second > ttl_ms_) {
            seen_hashes_.erase(expiry_queue_.front().first);
            expiry_queue_.pop_front();
        }
        
        // Проверяем replay
        if (seen_hashes_.count(hash_str)) {
            return true;  // Replay!
        }
        
        seen_hashes_.insert(hash_str);
        expiry_queue_.emplace_back(hash_str, now_ms);
        return false;
    }
};

// В peel_onion():
static OnionReplayFilter replay_filter;
if (replay_filter.is_replay(onion, get_current_time_ms())) {
    return std::unexpected(OnionError::Replay);
}
```

---

### 6. Небезопасное Хранение Ключей в Памяти

**Файл:** `client/Backend.h:354-363`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
struct Session {
    QString file;
    QString name;
    QString fingerprint;
    QString nodeId;
    QString shareString;
    aeromesh::Identity identity;  // ❌ Секретный ключ в обычной памяти!
    aeromesh::AccountVault::Key key;  // ❌ Ключ шифрования в памяти!
    aeromesh::AccountVault::Salt salt;
};

std::vector<std::unique_ptr<Session>> sessions_;  // ❌ Может быть в swap!
```

**Атака:**
1. Память процесса может быть записана в swap файл
2. Swap файл остается на диске после завершения программы
3. Злоумышленник с доступом к диску извлекает ключи из swap
4. Все сообщения расшифрованы

**Последствия:**
- Ключи могут быть извлечены из swap файла
- Ключи могут быть извлечены через memory dump
- Полная компрометация всех сообщений

**Исправление:**
```cpp
// 1. Использовать mlock для предотвращения swap
#include <sys/mman.h>

template<typename T>
class SecureMemory {
private:
    T* data_;
    size_t size_;
    
public:
    SecureMemory(size_t count) : size_(count * sizeof(T)) {
        data_ = static_cast<T*>(sodium_malloc(size_));
        if (!data_) throw std::bad_alloc();
        
        // Предотвращаем swap
        if (sodium_mlock(data_, size_) != 0) {
            sodium_free(data_);
            throw std::runtime_error("mlock failed");
        }
    }
    
    ~SecureMemory() {
        if (data_) {
            sodium_munlock(data_, size_);
            sodium_free(data_);
        }
    }
    
    T* get() { return data_; }
    // Запретить копирование
    SecureMemory(const SecureMemory&) = delete;
    SecureMemory& operator=(const SecureMemory&) = delete;
};

// 2. Изменить Session
struct Session {
    QString file;
    QString name;
    QString fingerprint;
    QString nodeId;
    QString shareString;
    SecureMemory<aeromesh::Identity> identity;  // ✅ Защищенная память
    SecureMemory<aeromesh::AccountVault::Key> key;  // ✅ Защищенная память
    aeromesh::AccountVault::Salt salt;
};

// 3. Добавить автоматическую блокировку после таймаута
class AutoLockTimer : public QTimer {
    Q_OBJECT
public:
    AutoLockTimer(Backend* backend, int timeout_minutes = 15) {
        setInterval(timeout_minutes * 60 * 1000);
        setSingleShot(true);
        connect(this, &QTimer::timeout, backend, &Backend::lock);
    }
};
```

---

### 7. Отсутствие Аутентификации в Relay Protocol

**Файл:** `core/src/relay.cpp:73-105`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
bool RelayServer::on_datagram(const Endpoint& from,
                              std::span<const std::byte> data) {
    const auto parsed = parse_relay_message(data);
    if (!parsed) {
        ++stats_.dropped;
        return false;
    }

    if (parsed->type == RelayMessageType::Bind) {
        Allocation& alloc = allocations_[parsed->session];
        // ❌ Любой может забиндиться к любой сессии!
        if (alloc.count >= alloc.peers.size()) {
            ++stats_.dropped;
            return true;
        }
        alloc.peers[alloc.count] = from;
        ++alloc.count;
        // ...
    }
}
```

**Атака:**
1. Злоумышленник узнает `session_id` (32 случайных байта)
2. Отправляет Bind с этим `session_id` на relay
3. Становится третьим участником сессии
4. Перехватывает все сообщения между двумя пользователями

**Последствия:**
- MitM атака через relay
- Перехват всех сообщений
- Возможность модификации трафика

**Исправление:**
```cpp
// 1. Добавить аутентификацию в Bind
struct AuthenticatedBind {
    RelaySessionId session;
    std::array<std::byte, 32> ephemeral_pub;  // Временный публичный ключ
    std::array<std::byte, 64> signature;  // Подпись от долгосрочного ключа
    uint64_t timestamp;
};

// 2. Relay проверяет подпись
bool RelayServer::verify_bind(const AuthenticatedBind& bind) {
    // Проверяем, что подпись валидна
    auto transcript = make_bind_transcript(bind.session, 
                                          bind.ephemeral_pub, 
                                          bind.timestamp);
    
    // Публичный ключ должен быть известен заранее (из DHT или handshake)
    auto expected_pubkey = get_expected_pubkey(bind.session);
    if (!expected_pubkey) return false;
    
    return Identity::verify(*expected_pubkey, transcript, bind.signature);
}

// 3. Ограничить количество участников сессии
if (alloc.count >= 2) {  // Только 2 участника!
    return false;
}
```

---

### 8. Утечка Метаданных через Cover Traffic Patterns

**Файл:** `core/src/cover.cpp:24-35`  
**Серьезность:** 🔴 КРИТИЧЕСКАЯ

**Проблема:**
```cpp
std::uint64_t CoverScheduler::sample_delay() noexcept {
    // splitmix64: детерминированный RNG
    rng_state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = rng_state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    const std::uint64_t span = max_interval_ms_ - min_interval_ms_ + 1;
    return min_interval_ms_ + (z % span);  // ❌ Предсказуемый паттерн!
}
```

**Атака:**
1. Злоумышленник наблюдает timing паттерн cover traffic
2. Детерминированный RNG создает повторяющиеся паттерны
3. Можно отличить реальные пакеты от dummy по статистическому анализу
4. Утечка информации о том, когда отправляются реальные сообщения

**Последствия:**
- Traffic analysis может отличить реальные пакеты от cover
- Утечка метаданных о времени коммуникации
- Деградация защиты от timing attacks

**Исправление:**
```cpp
std::uint64_t CoverScheduler::sample_delay() noexcept {
    // Использовать CSPRNG вместо детерминированного RNG
    std::array<unsigned char, 8> random_bytes;
    randombytes_buf(random_bytes.data(), random_bytes.size());
    
    uint64_t random_value;
    std::memcpy(&random_value, random_bytes.data(), sizeof(random_value));
    
    const std::uint64_t span = max_interval_ms_ - min_interval_ms_ + 1;
    return min_interval_ms_ + (random_value % span);
}

// Альтернатива: добавить случайный jitter
std::uint64_t add_jitter(std::uint64_t base_delay) {
    std::array<unsigned char, 2> jitter_bytes;
    randombytes_buf(jitter_bytes.data(), jitter_bytes.size());
    
    uint16_t jitter;
    std::memcpy(&jitter, jitter_bytes.data(), sizeof(jitter));
    
    // ±10% случайный jitter
    int64_t jitter_ms = (jitter % (base_delay / 5)) - (base_delay / 10);
    return base_delay + jitter_ms;
}
```

---

## 🟠 ВЫСОКИЕ УЯЗВИМОСТИ

### 9. Integer Overflow в Packet Decoding

**Файл:** `core/src/packet.cpp:48-62`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
std::expected<Packet, FrameError> decode(
    std::span<const std::byte, kFrameSize> frame) {
    const auto len = static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(frame[1]) << 8) |
        std::to_integer<std::uint16_t>(frame[2]));
    if (len > kMaxPayloadSize) {  // ❌ Проверка ПОСЛЕ приведения типа!
        return std::unexpected(FrameError::LengthMismatch);
    }
    // ...
    pkt.payload.assign(frame.begin() + kHeaderSize,
                       frame.begin() + kHeaderSize + len);  // ❌ Может выйти за границы!
}
```

**Исправление:**
```cpp
std::expected<Packet, FrameError> decode(
    std::span<const std::byte, kFrameSize> frame) {
    // Проверяем границы ДО использования
    const std::uint16_t len = static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(frame[1]) << 8) |
        std::to_integer<std::uint16_t>(frame[2]));
    
    if (len > kMaxPayloadSize) {
        return std::unexpected(FrameError::LengthMismatch);
    }
    
    // Дополнительная проверка
    if (kHeaderSize + len > kFrameSize) {
        return std::unexpected(FrameError::LengthMismatch);
    }
    
    Packet pkt;
    pkt.type = static_cast<PacketType>(raw);
    pkt.payload.assign(frame.begin() + kHeaderSize,
                       frame.begin() + kHeaderSize + len);
    return pkt;
}
```

---

### 10. Unbounded Memory Growth в Ratchet Skipped Keys

**Файл:** `core/src/ratchet.cpp:189-202`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
std::expected<void, RatchetError> Ratchet::skip_message_keys(std::uint32_t until) {
    if (!have_ckr_)
        return {};
    if (until > nr_ + kRatchetMaxSkip)  // kRatchetMaxSkip = 1000
        return std::unexpected(RatchetError::TooManySkipped);
    while (nr_ < until) {
        RatchetKey mk;
        RatchetKey next_ck;
        kdf_ck(ckr_, next_ck, mk);
        ckr_ = next_ck;
        skipped_.emplace(std::make_pair(dhr_, nr_), mk);  // ❌ Неограниченный рост!
        ++nr_;
    }
    return {};
}
```

**Атака:**
1. Злоумышленник отправляет сообщения с большими пропусками в `n`
2. `skipped_` map растет до 1000 ключей на каждый DH ratchet
3. При множественных DH ratchets память растет неограниченно
4. DoS через истощение памяти

**Исправление:**
```cpp
class Ratchet {
private:
    static constexpr size_t kMaxSkippedTotal = 2000;  // Глобальный лимит
    std::unordered_map<std::pair<RatchetKey, std::uint32_t>, RatchetKey> skipped_;
    
    void enforce_skipped_limit() {
        if (skipped_.size() > kMaxSkippedTotal) {
            // Удаляем самые старые ключи (FIFO)
            // Нужно добавить timestamp к каждому ключу
            auto oldest = std::min_element(skipped_.begin(), skipped_.end(),
                [](const auto& a, const auto& b) {
                    return a.second.timestamp < b.second.timestamp;
                });
            skipped_.erase(oldest);
        }
    }
    
public:
    std::expected<void, RatchetError> skip_message_keys(std::uint32_t until) {
        // ... существующая логика
        while (nr_ < until) {
            // ...
            skipped_.emplace(std::make_pair(dhr_, nr_), mk);
            enforce_skipped_limit();  // ✅ Ограничиваем размер
            ++nr_;
        }
        return {};
    }
};
```

---

### 11. Отсутствие Валидации в Wire Decoder

**Файл:** `client/Wire.h:60-90`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos = 0;
    bool ok = true;

    std::vector<std::byte> read_len_prefixed() {
        const std::uint16_t n = u16();  // ❌ Может быть огромным!
        if (!remaining(n)) {
            ok = false;
            return {};
        }
        const auto sub = data.subspan(pos, n);
        pos += n;
        return std::vector<std::byte>(sub.begin(), sub.end());  // ❌ Аллокация без лимита!
    }
};
```

**Атака:**
1. Злоумышленник отправляет пакет с `length = 65535`
2. `read_len_prefixed()` пытается аллоцировать 64KB
3. Множественные такие пакеты истощают память
4. DoS через memory exhaustion

**Исправление:**
```cpp
struct Reader {
    static constexpr size_t kMaxFieldSize = 8192;  // 8KB лимит
    
    std::vector<std::byte> read_len_prefixed() {
        const std::uint16_t n = u16();
        
        // Проверяем разумный лимит
        if (n > kMaxFieldSize) {
            ok = false;
            return {};
        }
        
        if (!remaining(n)) {
            ok = false;
            return {};
        }
        
        const auto sub = data.subspan(pos, n);
        pos += n;
        return std::vector<std::byte>(sub.begin(), sub.end());
    }
};
```

---

### 12. Отсутствие Timeout в HolePuncher

**Файл:** `core/src/holepunch.cpp:42-56`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
void HolePuncher::tick(std::uint64_t now_ms) {
    if (state_ != PunchState::Punching) {
        return;
    }
    if (now_ms - start_ms_ >= config_.timeout_ms) {
        state_ = PunchState::Failed;
        return;
    }
    if (!first_probe_sent_ || (now_ms - last_send_ms_) >= config_.interval_ms) {
        send_probe(now_ms);  // ❌ Отправляет бесконечно до timeout!
    }
}
```

**Проблема:**
- Нет лимита на количество попыток
- Может отправить сотни пакетов за timeout период
- Расходует bandwidth и создает шум в сети

**Исправление:**
```cpp
struct PunchConfig {
    // ... существующие поля
    size_t max_probes = 10;  // Максимум 10 попыток
};

class HolePuncher {
private:
    size_t probes_sent_ = 0;
    
public:
    void tick(std::uint64_t now_ms) {
        if (state_ != PunchState::Punching) {
            return;
        }
        
        // Проверяем лимит попыток
        if (probes_sent_ >= config_.max_probes) {
            state_ = PunchState::Failed;
            return;
        }
        
        if (now_ms - start_ms_ >= config_.timeout_ms) {
            state_ = PunchState::Failed;
            return;
        }
        
        if (!first_probe_sent_ || (now_ms - last_send_ms_) >= config_.interval_ms) {
            send_probe(now_ms);
        }
    }
};
```

---

### 13. Weak Password Policy

**Файл:** `core/include/aeromesh/account_vault.hpp`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
static constexpr std::size_t kMinPasswordLength = 8;  // ❌ Слишком слабо!
```

**Атака:**
1. Пользователь выбирает пароль "password"
2. Даже с Argon2id, короткий пароль уязвим к словарным атакам
3. Злоумышленник с доступом к зашифрованному файлу может подобрать пароль

**Исправление:**
```cpp
// account_vault.hpp
static constexpr std::size_t kMinPasswordLength = 12;  // Минимум 12 символов

// Backend.h - добавить проверку сложности
QString Backend::createAccount(const QString& name, const QString& password) {
    // Проверка длины
    if (static_cast<std::size_t>(password.toUtf8().size()) < 
        aeromesh::AccountVault::kMinPasswordLength) {
        return setError(QStringLiteral("weak"));
    }
    
    // Проверка сложности
    if (!is_password_strong(password)) {
        return setError(QStringLiteral("weak_complexity"));
    }
    
    // ... остальная логика
}

bool is_password_strong(const QString& password) {
    bool has_upper = false, has_lower = false, has_digit = false, has_special = false;
    
    for (const QChar& c : password) {
        if (c.isUpper()) has_upper = true;
        if (c.isLower()) has_lower = true;
        if (c.isDigit()) has_digit = true;
        if (!c.isLetterOrNumber()) has_special = true;
    }
    
    // Требуем минимум 3 из 4 категорий
    int categories = has_upper + has_lower + has_digit + has_special;
    return categories >= 3;
}
```

---

### 14. Отсутствие Проверки Подписи в Session Handshake

**Файл:** `core/src/session.cpp:82-95`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
std::expected<std::pair<SecureSession, SessionInitiation>, SessionError>
SecureSession::initiate(const Identity& self,
                        const Identity::PublicKey& expected_peer,
                        const SignedPrekeyBundle& bundle) {
    // Проверяем подпись bundle
    if (!Identity::verify(
            bundle.identity_pub, bundle_transcript(bundle),
            std::span<const std::byte, kSignatureLen>(bundle.signature))) {
        return std::unexpected(SessionError::BadSignature);
    }
    // ❌ НО: не проверяем, что bundle.identity_pub соответствует expected_peer!
    // Проверка есть, но ПОСЛЕ verify, что неэффективно
}
```

**Исправление:**
```cpp
std::expected<std::pair<SecureSession, SessionInitiation>, SessionError>
SecureSession::initiate(const Identity& self,
                        const Identity::PublicKey& expected_peer,
                        const SignedPrekeyBundle& bundle) {
    // ✅ СНАЧАЛА проверяем identity
    if (bundle.identity_pub != expected_peer) {
        return std::unexpected(SessionError::BadSignature);
    }
    
    // Затем проверяем подпись
    if (!Identity::verify(
            bundle.identity_pub, bundle_transcript(bundle),
            std::span<const std::byte, kSignatureLen>(bundle.signature))) {
        return std::unexpected(SessionError::BadSignature);
    }
    
    // ... остальная логика
}
```

---

### 15. UDP Socket без Проверки Source Address

**Файл:** `platform/src/udp_socket.cpp:145-175`  
**Серьезность:** 🟠 ВЫСОКАЯ

**Проблема:**
```cpp
bool UdpSocket::poll(Endpoint& from, std::vector<std::byte>& out) {
    // ...
    const auto n = ::recvfrom(to_socket(fd_),
                              reinterpret_cast<char*>(buf.data()),
                              static_cast<int>(buf.size()), 0,
                              reinterpret_cast<sockaddr*>(&src), &src_len);
    // ❌ Принимаем пакеты от ЛЮБОГО адреса!
    
    // ...
    from.host = host;
    from.port = static_cast<std::uint16_t>(std::strtoul(serv, nullptr, 10));
    out.assign(buf.begin(), buf.begin() + n);
    return true;  // ❌ Нет фильтрации по whitelist!
}
```

**Атака:**
1. Злоумышленник отправляет пакеты с поддельным source IP (IP spoofing)
2. Приложение обрабатывает их как легитимные
3. Возможны amplification атаки и DoS

**Исправление:**
```cpp
class UdpSocket {
private:
    std::unordered_set<std::string> allowed_peers_;
    bool whitelist_enabled_ = false;
    
public:
    void enable_whitelist(bool enable) {
        whitelist_enabled_ = enable;
    }
    
    void add_allowed_peer(const std::string& endpoint) {
        allowed_peers_.insert(endpoint);
    }
    
    bool poll(Endpoint& from, std::vector<std::byte>& out) {
        // ... существующая логика получения пакета
        
        from.host = host;
        from.port = static_cast<std::uint16_t>(std::strtoul(serv, nullptr, 10));
        
        // Проверяем whitelist
        if (whitelist_enabled_) {
            std::string endpoint = from.host + ":" + std::to_string(from.port);
            if (allowed_peers_.find(endpoint) == allowed_peers_.end()) {
                return false;  // Отклоняем пакет от неизвестного источника
            }
        }
        
        out.assign(buf.begin(), buf.begin() + n);
        return true;
    }
};
```

---

### 16-23. Дополнительные Высокие Уязвимости

**16. Отсутствие Forward Secrecy в Onion Routing**
- Файл: `core/src/onion.cpp`
- Проблема: Используется `crypto_box_seal` без ephemeral ключей
- Исправление: Использовать ephemeral ключи для каждого onion пакета

**17. Отсутствие Проверки Timestamp в Handshake**
- Файл: `core/src/session.cpp`
- Проблема: Нет проверки свежести bundle/initiation
- Исправление: Добавить timestamp и проверять ±5 минут

**18. Утечка через QML Properties**
- Файл: `client/Backend.h:60-64`
- Проблема: `shareString` и `fingerprint` доступны из QML
- Исправление: Ограничить доступ, добавить rate limiting

**19. Отсутствие Проверки Certificate Pinning**
- Файл: Отсутствует
- Проблема: Нет pinning для bootstrap узлов
- Исправление: Добавить hardcoded публичные ключи bootstrap узлов

**20. Weak Entropy Source для Session IDs**
- Файл: `core/src/relay.cpp:28`
- Проблема: `randombytes_buf` может быть предсказуем на некоторых платформах
- Исправление: Использовать `/dev/urandom` напрямую на Unix

**21. Отсутствие Padding в Ratchet Messages**
- Файл: `core/src/ratchet.cpp`
- Проблема: Размер ciphertext раскрывает размер plaintext
- Исправление: Добавить padding до фиксированных размеров (256, 512, 1024 байт)

**22. Отсутствие Protection от Traffic Analysis**
- Файл: `core/src/transport.cpp`
- Проблема: Cover traffic не защищает от correlation attacks
- Исправление: Добавить случайные задержки и batching

**23. Отсутствие Secure Delete для Account Files**
- Файл: `client/Backend.h:270-292`
- Проблема: `QFile::remove()` не затирает данные на диске
- Исправление: Перезаписать файл случайными данными перед удалением

---

## 🟡 СРЕДНИЕ ПРОБЛЕМЫ И АРХИТЕКТУРНЫЕ НЕДОСТАТКИ

### Отсутствие Audit Logging
- Нет логирования критических событий (handshake failures, replay attempts)
- Невозможно обнаружить атаки post-factum

### Отсутствие Secure Boot Verification
- Нет проверки целостности бинарника при запуске
- Возможна подмена исполняемого файла

### Отсутствие Anti-Debugging
- Нет защиты от отладчиков и reverse engineering
- Ключи могут быть извлечены через debugger

### Отсутствие Code Obfuscation
- Код легко читается и анализируется
- Упрощает поиск уязвимостей

### Отсутствие Secure Enclave Support
- Нет использования TPM/Secure Enclave для хранения ключей
- Ключи хранятся в обычной памяти

---

## РЕКОМЕНДАЦИИ ПО ПРИОРИТЕТАМ

### Немедленно (Критические):
1. ✅ Добавить аутентификацию узлов в DHT (защита от Sybil)
2. ✅ Исправить timing attack в Ratchet
3. ✅ Добавить rate limiting для DHT запросов
4. ✅ Добавить ЯВНОЕ предупреждение об утечке IP через STUN
5. ✅ Добавить replay protection в onion routing
6. ✅ Защитить ключи в памяти (mlock)
7. ✅ Добавить аутентификацию в relay protocol
8. ✅ Исправить cover traffic RNG

### В течение недели (Высокие):
9. Исправить integer overflow в packet decoding
10. Ограничить рост skipped keys в ratchet
11. Добавить валидацию в wire decoder
12. Добавить timeout в holepuncher
13. Усилить password policy
14. Исправить порядок проверок в handshake
15. Добавить whitelist для UDP socket

### В течение месяца (Средние):
16-23. Остальные высокие уязвимости
24. Добавить audit logging
25. Добавить secure delete
26. Добавить padding в messages
27. Улучшить защиту от traffic analysis

---

## ОБЩИЕ РЕКОМЕНДАЦИИ

### 1. Threat Model
- Обновить `THREAT_MODEL.md` с реальными ограничениями
- Добавить раздел "What We DON'T Protect Against"
- Явно указать, что требуется Tor/VPN для IP анонимности

### 2. Security Audit
- Провести независимый аудит криптографии
- Провести penetration testing
- Получить сертификацию (если планируется коммерческое использование)

### 3. Documentation
- Добавить Security Best Practices для пользователей
- Создать Incident Response Plan
- Документировать все криптографические решения

### 4. Testing
- Добавить fuzzing для всех парсеров
- Добавить property-based testing для криптографии
- Добавить integration tests для attack scenarios

### 5. Monitoring
- Добавить telemetry для обнаружения атак (opt-in)
- Добавить crash reporting (с защитой приватности)
- Мониторить bootstrap узлы на предмет компрометации

---

## ЗАКЛЮЧЕНИЕ

AeroMesh имеет **серьезные уязвимости безопасности**, которые делают его **небезопасным для использования в текущем состоянии**. Особенно критичны:

1. **Отсутствие защиты от Sybil атак в DHT** — позволяет полную деанонимизацию
2. **Утечка IP через STUN** — нарушает основное обещание анонимности
3. **Отсутствие аутентификации в relay** — позволяет MitM атаки
4. **Небезопасное хранение ключей** — ключи могут быть извлечены из памяти

**Рекомендация:** НЕ использовать в production до исправления всех критических уязвимостей и проведения независимого аудита.

**Для разработчиков:** Следуйте принципу "Security by Design" — добавляйте защиту на этапе проектирования, а не постфактум.

**Для пользователей:** Если вы используете AeroMesh, ОБЯЗАТЕЛЬНО:
- Используйте Tor или VPN
- Не доверяйте ему критически важную информацию
- Помните, что это экспериментальный проект

---

**Контакт для вопросов:** [security@aeromesh.example]  
**Дата следующего аудита:** После исправления критических уязвимостей

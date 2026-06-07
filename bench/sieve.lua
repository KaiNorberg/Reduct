local function sieve(n)
    local is_prime = {}
    for i = 2, n do
        is_prime[i] = true
    end

    for i = 2, math.sqrt(n) do
        if is_prime[i] then
            for j = i * i, n, i do
                is_prime[j] = false
            end
        end
    end

    local primes = {}
    for i = 2, n do
        if is_prime[i] then
            table.insert(primes, i)
        end
    end

    return primes
end

local primes = sieve(10000)
print(primes)

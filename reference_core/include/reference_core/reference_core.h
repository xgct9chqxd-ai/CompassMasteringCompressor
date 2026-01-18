#pragma once

#include <cmath>
#include <cstdint>

namespace reference_core
{
    constexpr const char* kReferenceCoreId = "reference_core_v1";

    // Phase 1.1 — Canonical Signal Domains (LOCKED)
    //
    // 1) Detector domain (DetectorLin):
    //    - Oversampled reconstruction domain used for true-peak style measurement.
    //    - Linear amplitude (not dB). Measurement helpers convert DetectorLin -> dBTP.
    //
    // 2) Control domain (ControlDb):
    //    - All GR and envelope values live here, in dB (attenuation is >= 0 dB).
    //
    // 3) Apply domain (ApplyLin):
    //    - Final authoritative scaling domain applied to samples at the end.
    //    - Linear amplitude scale factor, derived from ControlDb.

    enum class Domain : std::uint8_t
    {
        DetectorLin = 0,
        ControlDb   = 1,
        ApplyLin    = 2
    };

    constexpr double kDbEpsLin = 1.0e-20;

    // Phase 1.1 — Domain Audit Hooks (TEST-ONLY) (LOCKED)
    // Instrumentation points only. This does not “detect all illegal math” automatically.
    // It provides counters for explicit audit touchpoints used by tests.

#if defined(REFERENCE_CORE_ENABLE_DOMAIN_AUDIT) && (REFERENCE_CORE_ENABLE_DOMAIN_AUDIT == 1)

    struct DomainAudit final
    {
        std::uint32_t linearToDbCalls = 0;
        std::uint32_t controlStoresNotDb = 0;

        void reset() noexcept
        {
            linearToDbCalls = 0;
            controlStoresNotDb = 0;
        }
    };

    inline DomainAudit& domainAudit() noexcept
    {
        static DomainAudit a;
        return a;
    }

    inline void auditControlSignalStore (Domain storedDomain) noexcept
    {
        if (storedDomain != Domain::ControlDb)
            ++domainAudit().controlStoresNotDb;
    }

#else

    struct DomainAudit final
    {
        void reset() noexcept {}
    };

    inline DomainAudit& domainAudit() noexcept
    {
        static DomainAudit a;
        return a;
    }

    inline void auditControlSignalStore (Domain) noexcept {}

#endif

    // Phase 1.1 — Conversion Utilities (SANCTIONED) (LOCKED)
    // Control domain uses attenuation dB (attnDb >= 0). Apply domain uses linear scale.
    // dBTP ceiling is amplitude-referenced (may be <= 0).

    inline double dbToLinear (double attnDb) noexcept
    {
        if (! std::isfinite (attnDb)) return 0.0;
        if (attnDb <= 0.0) return 1.0;
        return std::pow (10.0, -attnDb / 20.0);
    }

    inline double linearToDb (double scaleLin) noexcept
    {
#if defined(REFERENCE_CORE_ENABLE_DOMAIN_AUDIT) && (REFERENCE_CORE_ENABLE_DOMAIN_AUDIT == 1)
        ++domainAudit().linearToDbCalls;
#endif
        if (! std::isfinite (scaleLin) || scaleLin <= 0.0) return 120.0;
        return -20.0 * std::log10 (scaleLin + kDbEpsLin);
    }

    inline double dbtpToLinearCeiling (double ceilingDbtp) noexcept
    {
        if (! std::isfinite (ceilingDbtp)) return 0.0;
        return std::pow (10.0, ceilingDbtp / 20.0);
    }
}

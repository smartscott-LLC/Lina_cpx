-- =============================================================================
-- LINA_SCHEMA.SQL — 14 Tables for PostgreSQL + pgvector
--
-- LINA Core Substrate · "Safe by design. Not safe by limitation."
--
-- Contract: blueprint §6 (authoritative). Reconciliations:
--   D-002  fall seed corrected (order_min 3.2, chaos_max 3.8)
--   D-010  `tier` column added to lina_memory_items
--   D-029  reference schema reviewed; blueprint contract stands
-- =============================================================================

-- Enable pgvector extension
CREATE EXTENSION IF NOT EXISTS vector;

-- ---------------------------------------------------------------------------
-- 1. lina_identity_core — user identity, season, founding context
-- ---------------------------------------------------------------------------
CREATE TABLE lina_identity_core (
    id INTEGER PRIMARY KEY DEFAULT 1 CHECK (id = 1), -- Lina is ONE entity (D-050)
    current_season VARCHAR(20) DEFAULT 'spring',
    relationship_depth VARCHAR(20) DEFAULT 'new',
    self_description TEXT,
    session_count INTEGER DEFAULT 0,
    total_evaluations INTEGER DEFAULT 0,
    alignment_rate DECIMAL(5,4) DEFAULT 0.0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO lina_identity_core (id) VALUES (1);

-- ---------------------------------------------------------------------------
-- 2. lina_polytope_constraints — 14D seasonal boundary definitions (seeded)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_polytope_constraints (
    id SERIAL PRIMARY KEY,
    season VARCHAR(20) NOT NULL,
    harmony_min DECIMAL(5,2),
    dominance_max DECIMAL(5,2),
    order_min DECIMAL(5,2),
    chaos_max DECIMAL(5,2),
    integrity_min DECIMAL(5,2),
    deception_max DECIMAL(5,2),
    flourishing_min DECIMAL(5,2),
    decline_max DECIMAL(5,2),
    relationships_min DECIMAL(5,2),
    isolation_max DECIMAL(5,2),
    boundaries_min DECIMAL(5,2),
    intrusion_max DECIMAL(5,2),
    grace_min DECIMAL(5,2),
    rigidity_max DECIMAL(5,2),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 3. lina_memory_items — unified MPS store (tiers + long-term) with vector(14)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_memory_items (
    item_id TEXT PRIMARY KEY,
    narrative TEXT NOT NULL,
    hemisphere VARCHAR(20) DEFAULT 'personal',
    ethical_coordinates vector(14),
    importance_score DECIMAL(5,2),
    geometric DECIMAL(5,2),
    emotional_marker VARCHAR(20) DEFAULT 'neutral',
    emotional_intensity DECIMAL(5,2) DEFAULT 0.5,
    formation_source TEXT,
    seasonal_marker VARCHAR(20),
    concept_name TEXT,
    understanding TEXT,
    reflection TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    trigger BOOLEAN DEFAULT FALSE,
    kind VARCHAR(20) DEFAULT 'episodic',
    status VARCHAR(20) DEFAULT 'active',
    protected_flag BOOLEAN DEFAULT FALSE,
    failed_gate DECIMAL(5,2),
    entered_fallout_at TIMESTAMP,
    reference_count INTEGER DEFAULT 0,
    floor DECIMAL(5,2),
    must_keep BOOLEAN DEFAULT FALSE,
    last_referenced_at TIMESTAMP,
    decay_started_at TIMESTAMP,
    -- D-010: standing tier (t1/t2/t3/long_term); status stays the lifecycle field
    tier VARCHAR(10) NOT NULL DEFAULT 't1'
);

-- Vector index for similarity search (cosine)
CREATE INDEX idx_memory_ethical ON lina_memory_items
    USING ivfflat (ethical_coordinates vector_cosine_ops);

-- ---------------------------------------------------------------------------
-- 4. lina_transcripts — permanent conversation archive (cognitive bus)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_transcripts (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role VARCHAR(20) NOT NULL,
    content TEXT NOT NULL,
    msg_type VARCHAR(20),
    evaluation_id TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 5. lina_sessions — session tracking
-- ---------------------------------------------------------------------------
CREATE TABLE lina_sessions (
    id TEXT PRIMARY KEY,
    session_number INTEGER NOT NULL,
    season VARCHAR(20) DEFAULT 'spring',
    depth VARCHAR(20) DEFAULT 'new',
    finalized BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    finalized_at TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 6. lina_actions — human-in-the-loop action audit ledger
-- ---------------------------------------------------------------------------
CREATE TABLE lina_actions (
    id TEXT PRIMARY KEY,
    tool_name VARCHAR(100) NOT NULL,
    params_json JSONB,
    state VARCHAR(20) DEFAULT 'pending',
    result TEXT,
    error TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 7. lina_memory_promotions — memory promotion log (growth leaves its mark)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_memory_promotions (
    id SERIAL PRIMARY KEY,
    item_id TEXT NOT NULL,
    from_stage VARCHAR(20) NOT NULL,
    to_stage VARCHAR(20) NOT NULL,
    score DECIMAL(5,2),
    reason TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 8. lina_evaluations — polytope evaluation & alignment history
-- ---------------------------------------------------------------------------
CREATE TABLE lina_evaluations (
    id SERIAL PRIMARY KEY,
    session_id TEXT NOT NULL,
    response_text TEXT,
    input_vector vector(14),
    output_vector vector(14),
    corrected_vector vector(14),
    is_aligned BOOLEAN,
    alignment_score DECIMAL(5,4),
    correction_magnitude DECIMAL(5,4),
    zone VARCHAR(20),
    season VARCHAR(20),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 9. lina_season_transitions — seasonal advancement log
-- ---------------------------------------------------------------------------
CREATE TABLE lina_season_transitions (
    id SERIAL PRIMARY KEY,
    from_season VARCHAR(20),
    to_season VARCHAR(20),
    trigger_event TEXT,
    transitioned_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 10. lina_wisdom_filters — reframing transformations applied
-- ---------------------------------------------------------------------------
CREATE TABLE lina_wisdom_filters (
    id SERIAL PRIMARY KEY,
    filter_name VARCHAR(100) NOT NULL,
    transform_pattern TEXT,
    active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 11. lina_working_memory — fast multi-turn conversation buffer
-- ---------------------------------------------------------------------------
CREATE TABLE lina_working_memory (
    id SERIAL PRIMARY KEY,
    session_id TEXT NOT NULL REFERENCES lina_sessions(id),
    turn_sequence INTEGER NOT NULL,
    role VARCHAR(20) NOT NULL,
    content TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 12. lina_fallout_buffer — 48-hour second-chance memory store (D-027)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_fallout_buffer (
    id TEXT PRIMARY KEY,
    narrative TEXT NOT NULL,
    importance_score DECIMAL(5,2),
    entered_fallout_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    original_tier VARCHAR(20)
);

-- ---------------------------------------------------------------------------
-- 13. lina_standing_grants — opt-in pre-authorized tool permissions
-- ---------------------------------------------------------------------------
CREATE TABLE lina_standing_grants (
    id SERIAL PRIMARY KEY,
    tool_pattern VARCHAR(100) NOT NULL,
    path_pattern VARCHAR(255) NOT NULL,
    granted BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------------
-- 14. lina_telemetry_logs — technical process & diagnostic stream (telemetry bus)
-- ---------------------------------------------------------------------------
CREATE TABLE lina_telemetry_logs (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    subsystem VARCHAR(50) NOT NULL,
    message TEXT NOT NULL,
    severity VARCHAR(20) DEFAULT 'INFO',
    latency_ms DECIMAL(10,2)
);

-- ---------------------------------------------------------------------------
-- Seed default seasonal constraints
-- Values are the C++ exact-rational table ×10 (D-002: operative source is
-- get_seasonal_bounds(); fall corrected to order_min 3.2, chaos_max 3.8).
-- ---------------------------------------------------------------------------
INSERT INTO lina_polytope_constraints (season, harmony_min, dominance_max, order_min, chaos_max, integrity_min, deception_max, flourishing_min, decline_max, relationships_min, isolation_max, boundaries_min, intrusion_max, grace_min, rigidity_max)
VALUES
    ('spring', 3.0, 5.0, 4.0, 3.0, 6.0, 2.0, 4.0, 3.0, 5.0, 4.0, 5.0, 3.0, 3.0, 5.0),
    ('summer', 2.8, 5.2, 3.8, 3.2, 6.0, 2.0, 3.8, 3.2, 4.8, 4.2, 4.8, 3.2, 2.8, 5.2),
    ('fall', 2.2, 5.8, 3.2, 3.8, 5.5, 2.5, 3.2, 3.8, 4.2, 4.8, 4.2, 3.8, 2.2, 5.8),
    ('winter', 1.8, 6.0, 2.8, 4.2, 6.5, 1.5, 2.8, 4.2, 3.8, 5.2, 3.8, 4.2, 1.8, 6.2);

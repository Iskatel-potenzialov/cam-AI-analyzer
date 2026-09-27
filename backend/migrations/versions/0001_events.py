from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql
revision="0001_events";down_revision=None
def upgrade():
 op.create_table("events",sa.Column("event_id",postgresql.UUID(as_uuid=True),primary_key=True),sa.Column("schema_version",sa.Integer(),nullable=False),sa.Column("timestamp",sa.DateTime(timezone=True),nullable=False),sa.Column("camera_id",sa.String(),nullable=False),sa.Column("event_type",sa.String(),nullable=False),sa.Column("rule_id",sa.String()),sa.Column("object_class",sa.String()),sa.Column("track_id",sa.Integer()),sa.Column("direction",sa.String()),sa.Column("confidence",sa.Float()),sa.Column("attributes",postgresql.JSONB(),nullable=False),sa.Column("created_at",sa.DateTime(timezone=True),server_default=sa.text("now()"),nullable=False));op.create_index("ix_events_timestamp","events",["timestamp"]);op.create_index("ix_events_camera_id","events",["camera_id"]);op.create_index("ix_events_event_type","events",["event_type"]);op.create_index("ix_events_rule_id","events",["rule_id"])
def downgrade(): op.drop_table("events")
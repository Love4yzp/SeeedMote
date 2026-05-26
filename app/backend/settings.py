from pathlib import Path

from pydantic import AliasChoices, Field
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """Backend config.

    Env vars accept both the SEEEDMOTE_* names (matching the gateway/firmware
    convention documented in app/README.md) and the bare MQTT_* names.
    """

    mqtt_broker: str = Field(
        default="localhost",
        validation_alias=AliasChoices("SEEEDMOTE_BROKER", "MQTT_BROKER"),
    )
    mqtt_port: int = Field(
        default=1883,
        validation_alias=AliasChoices("SEEEDMOTE_BROKER_PORT", "MQTT_PORT"),
    )
    mqtt_user: str | None = Field(
        default=None,
        validation_alias=AliasChoices("SEEEDMOTE_BROKER_USER", "MQTT_USER"),
    )
    mqtt_password: str | None = Field(
        default=None,
        validation_alias=AliasChoices("SEEEDMOTE_BROKER_PASS", "MQTT_PASSWORD"),
    )
    port: int = 3001
    mock: bool = False
    shoes_yaml: Path = Path(__file__).parent.parent / "shoes.yaml"

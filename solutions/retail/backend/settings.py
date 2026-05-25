from pathlib import Path
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    mqtt_broker: str = "localhost"
    mqtt_port: int = 1883
    mqtt_user: str | None = None
    mqtt_password: str | None = None
    port: int = 3001
    mock: bool = False
    shoes_yaml: Path = Path(__file__).parent.parent / "shoes.yaml"

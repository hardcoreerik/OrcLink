use orclink_protocol::{AdapterEnvelope, ProposalReceipt, Risk, TheOrcProposal};
use serde::{Deserialize, Serialize, de::DeserializeOwned};
use serde_json::Value;
use std::{error::Error, fmt};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ApiProtocol {
    pub major: u64,
    pub minor: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ApiCounts {
    pub nodes: u64,
    pub capabilities: u64,
    pub records: u64,
    pub proposals: u64,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ApiStatus {
    pub status: String,
    pub api_version: String,
    pub daemon_version: String,
    pub protocol: ApiProtocol,
    pub started_at: u64,
    pub uptime_seconds: u64,
    pub counts: ApiCounts,
    pub authority: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CapabilityView {
    pub name: String,
    pub schema_version: u64,
    pub risk_floor: Option<Risk>,
    pub invokable: bool,
    pub nodes: Vec<String>,
    pub online_nodes: Vec<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct TimelineEntry {
    pub sequence: u64,
    pub source: String,
    pub received_at: u64,
    pub record: AdapterEnvelope,
}

#[derive(Debug)]
pub struct ClientError {
    pub status: Option<u16>,
    pub code: Option<String>,
    pub message: String,
}

impl fmt::Display for ClientError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match (self.status, self.code.as_deref()) {
            (Some(status), Some(code)) => {
                write!(formatter, "HTTP {status} {code}: {}", self.message)
            }
            (Some(status), None) => write!(formatter, "HTTP {status}: {}", self.message),
            _ => formatter.write_str(&self.message),
        }
    }
}

impl Error for ClientError {}

#[derive(Clone)]
pub struct Client {
    base_url: String,
    agent: ureq::Agent,
}

impl Client {
    pub fn new(base_url: impl Into<String>) -> Result<Self, ClientError> {
        let base_url = base_url.into().trim_end_matches('/').to_owned();
        let authority = base_url.strip_prefix("http://").unwrap_or_default();
        let loopback = !authority.is_empty()
            && !authority.contains('/')
            && !authority.contains('@')
            && authority
                .split_once(':')
                .map(|(host, port)| {
                    matches!(host, "127.0.0.1" | "localhost")
                        && port.parse::<u16>().is_ok_and(|port| port != 0)
                })
                .unwrap_or_else(|| matches!(authority, "127.0.0.1" | "localhost"));
        if !loopback {
            return Err(ClientError {
                status: None,
                code: Some("NON_LOOPBACK_URL".into()),
                message: "OrcLink client URLs must be loopback HTTP endpoints".into(),
            });
        }
        let agent: ureq::Agent = ureq::Agent::config_builder()
            .http_status_as_error(false)
            .build()
            .into();
        Ok(Self { base_url, agent })
    }

    pub fn base_url(&self) -> &str {
        &self.base_url
    }

    pub fn status(&self) -> Result<ApiStatus, ClientError> {
        self.get("/api/v1/status")
    }

    pub fn nodes(&self) -> Result<Vec<AdapterEnvelope>, ClientError> {
        self.get("/api/v1/nodes")
    }

    pub fn node(&self, node_id: &str) -> Result<AdapterEnvelope, ClientError> {
        self.get(&format!("/api/v1/nodes/{node_id}"))
    }

    pub fn capabilities(&self) -> Result<Vec<CapabilityView>, ClientError> {
        self.get("/api/v1/capabilities")
    }

    pub fn operations(&self) -> Result<Vec<AdapterEnvelope>, ClientError> {
        self.get("/api/v1/operations")
    }

    pub fn timeline(&self, after: u64, limit: usize) -> Result<Vec<TimelineEntry>, ClientError> {
        self.get(&format!(
            "/api/v1/timeline?after={after}&limit={}",
            limit.min(1_000)
        ))
    }

    pub fn proposal(&self, proposal_id: &str) -> Result<ProposalReceipt, ClientError> {
        self.get(&format!("/api/v1/proposals/{proposal_id}"))
    }

    pub fn propose(&self, proposal: &TheOrcProposal) -> Result<ProposalReceipt, ClientError> {
        let url = format!("{}/api/v1/proposals", self.base_url);
        let mut response = self
            .agent
            .post(&url)
            .header("X-OrcLink-Client", "orclink-client/0.1")
            .send_json(proposal)
            .map_err(transport_error)?;
        decode(&mut response)
    }

    pub fn openapi(&self) -> Result<Value, ClientError> {
        self.get("/api/v1/openapi.json")
    }

    fn get<T: DeserializeOwned>(&self, path: &str) -> Result<T, ClientError> {
        let url = format!("{}{path}", self.base_url);
        let mut response = self
            .agent
            .get(&url)
            .header("X-OrcLink-Client", "orclink-client/0.1")
            .call()
            .map_err(transport_error)?;
        decode(&mut response)
    }
}

fn decode<T: DeserializeOwned>(
    response: &mut http::Response<ureq::Body>,
) -> Result<T, ClientError> {
    let status = response.status().as_u16();
    let value: Value = response
        .body_mut()
        .read_json()
        .map_err(|error| ClientError {
            status: Some(status),
            code: Some("INVALID_RESPONSE".into()),
            message: error.to_string(),
        })?;
    if status >= 400 {
        return Err(ClientError {
            status: Some(status),
            code: value["error"]["code"].as_str().map(str::to_owned),
            message: value["error"]["message"]
                .as_str()
                .unwrap_or("OrcLink request failed")
                .to_owned(),
        });
    }
    serde_json::from_value(value).map_err(|error| ClientError {
        status: Some(status),
        code: Some("INVALID_RESPONSE".into()),
        message: error.to_string(),
    })
}

fn transport_error(error: ureq::Error) -> ClientError {
    ClientError {
        status: None,
        code: Some("TRANSPORT_FAILURE".into()),
        message: error.to_string(),
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn requires_loopback_and_normalizes_trailing_slash() {
        assert!(super::Client::new("https://example.com").is_err());
        assert!(super::Client::new("http://127.0.0.1.evil.example").is_err());
        assert!(super::Client::new("http://localhost:0").is_err());
        let client = super::Client::new("http://127.0.0.1:4017/").unwrap();
        assert_eq!(client.base_url(), "http://127.0.0.1:4017");
    }
}

const baseUrl = process.env.API_URL || "http://127.0.0.1:4000";

async function main() {
  const exampleResponse = await fetch(`${baseUrl}/example`);
  if (!exampleResponse.ok) throw new Error("/example failed");
  const example = await exampleResponse.json();

  const runResponse = await fetch(`${baseUrl}/run`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(example)
  });
  if (!runResponse.ok) {
    throw new Error(`/run failed: ${await runResponse.text()}`);
  }
  const result = await runResponse.json();
  if (!result.metrics || !result.comparison) {
    throw new Error("/run response did not include metrics and comparison");
  }

  const generateResponse = await fetch(`${baseUrl}/generate`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ count: 5 })
  });
  if (!generateResponse.ok) throw new Error("/generate failed");
  const generated = await generateResponse.json();
  if (generated.subjects.length !== 5) throw new Error("/generate returned the wrong subject count");

  console.log("API smoke test passed.");
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});

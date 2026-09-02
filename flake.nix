{
  description = "Logos Workflow Scheduler - Workflow deployment, cron scheduling, and webhook triggers";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows = "logos-module-builder/nixpkgs";

    # Module dependencies (mirrors metadata.json#dependencies)
    logos-workflow-engine = {
      # On the modernize branch until it merges: master still carries the
      # pre-universal engine, which publishes no .lidl contract.
      url = "github:corpetty/logos-workflow-engine/modernize-phase0";
      inputs.logos-module-builder.follows = "logos-module-builder";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, logos-module-builder, nixpkgs, logos-workflow-engine }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      # `flakeInputs`, keyed by the DEPENDENCY name in metadata.json — the
      # builder filters this set by those names (lib/mkLogosModule.nix).
      # `moduleInputs` is not an argument mkLogosModule accepts.
      flakeInputs = {
        workflow_engine = logos-workflow-engine;
      };
    };
}

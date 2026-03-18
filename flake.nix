{
  description = "Logos Workflow Scheduler - Workflow deployment, cron scheduling, and webhook triggers";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nixpkgs.follows = "logos-module-builder/nixpkgs";

    # Module dependencies (from module.yaml)
    logos-workflow-engine = {
      url = "github:corpetty/logos-workflow-engine";
      inputs.logos-module-builder.follows = "logos-module-builder";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, logos-module-builder, nixpkgs, logos-workflow-engine }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./module.yaml;
      moduleInputs = {
        workflow_engine = logos-workflow-engine;
      };
    };
}

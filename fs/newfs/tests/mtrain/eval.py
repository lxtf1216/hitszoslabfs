# Evaluate a model already trained by train.py
import engine
import utils
import model as m
import torch

EPOCHS = 5

path_prefix = "./"

def main():
    model_path = f"{path_prefix}model/model.pth"
    
    train_set, test_set = utils.merge_subsets(10, f"{path_prefix}trimmed")
    train_loader = utils.load(train_set)
    test_loader = utils.load(test_set)
    
    utils.info("train.py: data loaded")
    
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = m.TinyVGG()
    model.load_state_dict(torch.load(model_path))
    model.to(device)
    utils.info(f"train.py: using device: {device}")
    
    test_loss, test_acc = engine.test_step(
        model,
        test_loader,
        torch.nn.CrossEntropyLoss(),
        device=device,
    )
    utils.info(f"test loss: {test_loss:.4f}, test acc: {test_acc:.4f}")
    
    utils.info("train.py: training complete")
    
if __name__ == "__main__":
    import sys, os
    path_prefix = f"{sys.argv[1]}/" if len(sys.argv) > 1 else "./"
    
    main()